#include "overlay/QueuePeek.h"

#include "Config.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace threnody::overlay {
namespace {

constexpr wchar_t peekClassName[] = L"ThrenodyQueuePeek";
constexpr UINT_PTR frameTimerId = 1;
constexpr unsigned frameMs = 16;

constexpr D2D1_COLOR_F toD2D(const Color& c) noexcept {
    return {c.r, c.g, c.b, c.a};
}

Result<winrt::com_ptr<IDWriteTextFormat>> makeFormat(IDWriteFactory2& dwrite, float size, DWRITE_FONT_WEIGHT weight) {
    winrt::com_ptr<IDWriteTextFormat> format;
    HRESULT hr = dwrite.CreateTextFormat(config::fontFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, size, L"", format.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateTextFormat(queue peek)");
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    winrt::com_ptr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(dwrite.CreateEllipsisTrimmingSign(format.get(), ellipsis.put()))) {
        const DWRITE_TRIMMING trimming{.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER};
        format->SetTrimming(&trimming, ellipsis.get());
    }
    return format;
}

}  // namespace

QueuePeek::QueuePeek(HINSTANCE instance, render::Graphics graphics)
    : m_instance(instance),
      m_graphics(std::move(graphics)),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &QueuePeek::windowProc,
          .hInstance = instance,
          .lpszClassName = peekClassName,
      }) {}

QueuePeek::~QueuePeek() = default;

Result<std::unique_ptr<QueuePeek>> QueuePeek::create(HINSTANCE instance) {
    Result<render::Graphics> graphics = render::Graphics::create();
    if (!graphics) {
        return graphics.error();
    }
    std::unique_ptr<QueuePeek> peek{new QueuePeek(instance, std::move(graphics.value()))};
    if (const Result<void> ready = peek->init(); !ready) {
        return ready.error();
    }
    return peek;
}

Result<void> QueuePeek::init() {
    if (!m_class.registered()) {
        return Error::fromLastError("RegisterClassEx(ThrenodyQueuePeek)");
    }
    m_hwnd.reset(CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 m_class.name(), L"Threnody up next", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                 m_instance, this));
    if (!m_hwnd) {
        return Error::fromLastError("CreateWindowEx(ThrenodyQueuePeek)");
    }

    IDWriteFactory2& dwrite = *m_graphics.dwrite;
    for (auto [format, size, weight] :
         {std::tuple{&m_labelFormat, config::queuePeekLabelSizeDip, DWRITE_FONT_WEIGHT_NORMAL},
          std::tuple{&m_titleFormat, config::queuePeekTitleSizeDip, DWRITE_FONT_WEIGHT_SEMI_BOLD},
          std::tuple{&m_subtitleFormat, config::queuePeekSubtitleSizeDip, DWRITE_FONT_WEIGHT_NORMAL}}) {
        Result<winrt::com_ptr<IDWriteTextFormat>> made = makeFormat(dwrite, size, weight);
        if (!made) {
            return made.error();
        }
        *format = std::move(made.value());
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties{
        .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
        .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
        .usage = D2D1_RENDER_TARGET_USAGE_NONE,
        .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
    };
    HRESULT hr = m_graphics.d2d->CreateDCRenderTarget(&properties, m_target.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateDCRenderTarget(queue peek)");
    }
    m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    hr = m_target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, m_brush.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateSolidColorBrush(queue peek)");
    }
    return {};
}

void QueuePeek::show(const RECT& anchor, UINT dpi, const Content& content) {
    if (!m_hwnd) {
        return;
    }
    const bool wasVisible = visible();
    if (wasVisible && content == m_content && dpi == m_dpi) {
        return;
    }
    m_content = content;
    m_dpi = dpi == 0 ? 96 : dpi;

    const int width = win32::scaleDip(static_cast<int>(config::queuePeekWidthDip), m_dpi);
    const int height = win32::scaleDip(static_cast<int>(config::queuePeekHeightDip), m_dpi);
    const int gap = win32::scaleDip(static_cast<int>(config::queuePeekGapDip), m_dpi);
    MONITORINFO monitor{.cbSize = sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& screen = monitor.rcMonitor;
    const RECT& work = monitor.rcWork;
    const int centreX = (anchor.left + anchor.right) / 2;
    const int left = std::clamp(centreX - width / 2, static_cast<int>(work.left), static_cast<int>(work.right) - width);
    const bool below = (anchor.top + anchor.bottom) / 2 < (screen.top + screen.bottom) / 2;
    const int top = below ? std::max<int>(anchor.bottom, work.top) + gap : std::min<int>(anchor.top, work.bottom) - gap - height;

    if (const Result<void> resized = m_surface.resize(SIZE{.cx = width, .cy = height}); !resized) {
        log::error("{}", resized.error().describe());
        return;
    }
    if (const Result<void> drawn = draw(); !drawn) {
        log::error("{}", drawn.error().describe());
        return;
    }
    SetWindowPos(m_hwnd.get(), HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
    if (!wasVisible) {
        m_alpha = 0;
        m_shownTick = GetTickCount64();
        SetTimer(m_hwnd.get(), frameTimerId, frameMs, nullptr);
    }
    frame();
    if (!wasVisible) {
        ShowWindow(m_hwnd.get(), SW_SHOWNOACTIVATE);
    }
}

void QueuePeek::hide() {
    if (visible()) {
        KillTimer(m_hwnd.get(), frameTimerId);
        ShowWindow(m_hwnd.get(), SW_HIDE);
    }
}

bool QueuePeek::visible() const noexcept {
    return m_hwnd && IsWindowVisible(m_hwnd.get());
}

void QueuePeek::frame() {
    const float t = static_cast<float>(GetTickCount64() - m_shownTick) / config::queuePeekFadeMs;
    m_alpha = static_cast<BYTE>(std::lround(std::clamp(t, 0.0f, 1.0f) * 255.0f));
    if (const Result<void> presented = m_surface.present(m_hwnd.get(), m_alpha); !presented) {
        log::error("{}", presented.error().describe());
    }
    if (m_alpha == 255) {
        KillTimer(m_hwnd.get(), frameTimerId);
    }
}

Result<winrt::com_ptr<ID2D1Bitmap>> QueuePeek::decodeCover() {
    IWICImagingFactory& wic = *m_graphics.wic;
    winrt::com_ptr<IWICStream> stream;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::com_ptr<IWICFormatConverter> converter;
    const UINT side = static_cast<UINT>(win32::scaleDip(static_cast<int>(config::queuePeekCoverDip), m_dpi));
    HRESULT hr = wic.CreateStream(stream.put());
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromMemory(m_content.cover.data(), static_cast<DWORD>(m_content.cover.size()));
    }
    if (SUCCEEDED(hr)) {
        hr = wic.CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    }
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, frame.put());
    }
    if (SUCCEEDED(hr)) {
        hr = wic.CreateBitmapScaler(scaler.put());
    }
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(frame.get(), side, side, WICBitmapInterpolationModeHighQualityCubic);
    }
    if (SUCCEEDED(hr)) {
        hr = wic.CreateFormatConverter(converter.put());
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut);
    }
    winrt::com_ptr<ID2D1Bitmap> bitmap;
    if (SUCCEEDED(hr)) {
        hr = m_target->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put());
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "queue peek cover");
    }
    return bitmap;
}

Result<void> QueuePeek::draw() {
    using namespace config;
    m_target->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    const SIZE size = m_surface.size();
    const RECT bounds{0, 0, size.cx, size.cy};
    if (const HRESULT hr = m_target->BindDC(m_surface.dc(), &bounds); FAILED(hr)) {
        return Error::fromHResult(hr, "BindDC(queue peek)");
    }
    const float width = queuePeekWidthDip;
    const float height = queuePeekHeightDip;
    const float pad = queuePeekPaddingDip;
    const auto fill = [&](const Color& color) { m_brush->SetColor(toD2D(color)); };

    winrt::com_ptr<ID2D1Bitmap> cover;
    if (!m_content.cover.empty()) {
        if (Result<winrt::com_ptr<ID2D1Bitmap>> decoded = decodeCover(); decoded) {
            cover = std::move(decoded.value());
        }
    }

    m_target->BeginDraw();
    m_target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
    fill(popupBackgroundColor);
    m_target->FillRoundedRectangle({{0.0f, 0.0f, width, height}, popupCornerRadiusDip, popupCornerRadiusDip},
                                   m_brush.get());
    fill(popupBorderColor);
    m_target->DrawRoundedRectangle(
        {{0.5f, 0.5f, width - 0.5f, height - 0.5f}, popupCornerRadiusDip, popupCornerRadiusDip},
        m_brush.get(), 1.0f);

    const float coverTop = (height - queuePeekCoverDip) / 2.0f;
    const D2D1_ROUNDED_RECT coverShape{{pad, coverTop, pad + queuePeekCoverDip, coverTop + queuePeekCoverDip},
                                       coverCornerRadiusDip, coverCornerRadiusDip};
    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
    if (cover && SUCCEEDED(m_graphics.d2d->CreateRoundedRectangleGeometry(coverShape, clip.put()))) {
        D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
        layer.geometricMask = clip.get();
        m_target->PushLayer(layer, nullptr);
        m_target->DrawBitmap(cover.get(), coverShape.rect);
        m_target->PopLayer();
    } else {
        fill(coverPlaceholderColor);
        m_target->FillRoundedRectangle(coverShape, m_brush.get());
    }

    const float textLeft = pad + queuePeekCoverDip + pad;
    const float textRight = width - pad;
    const auto line = [&](const std::wstring& text, IDWriteTextFormat& format, const Color& color, float top,
                          float lineHeight) {
        fill(color);
        m_target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), &format,
                            {textLeft, top, textRight, top + lineHeight}, m_brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const float labelHeight = queuePeekLabelSizeDip * 1.35f;
    const float titleHeight = queuePeekTitleSizeDip * 1.35f;
    const float subtitleHeight = queuePeekSubtitleSizeDip * 1.35f;
    const float top = (height - labelHeight - titleHeight - subtitleHeight) / 2.0f;
    line(m_content.label, *m_labelFormat, controlActiveColor, top, labelHeight);
    line(m_content.title, *m_titleFormat, titleColor, top + labelHeight, titleHeight);
    line(m_content.subtitle, *m_subtitleFormat, artistColor, top + labelHeight + titleHeight, subtitleHeight);

    const HRESULT hr = m_target->EndDraw();
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "EndDraw(queue peek)");
    }
    return {};
}

LRESULT CALLBACK QueuePeek::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<QueuePeek*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT QueuePeek::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == frameTimerId) {
                frame();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (m_hwnd.get() == hwnd) {
                m_hwnd.release();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace threnody::overlay
