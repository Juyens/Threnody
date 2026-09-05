#include "overlay/LockKeyOverlay.h"

#include "Config.h"
#include "render/SvgPath.h"
#include "util/Log.h"

#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <cmath>

namespace threnody::overlay {
namespace {

constexpr wchar_t overlayClassName[] = L"ThrenodyLockKeyOverlay";
constexpr UINT_PTR frameTimerId = 1;
constexpr unsigned frameMs = 16;

// Padlock, in a 24 x 24 box. Three parts so the shackle can move on its own.
constexpr std::string_view lockBodyPath =
    "M7.25 7A3.25 3.25 0 0 0 4 10.25v7.5C4 19.55 5.46 21 7.25 21h9.5c1.8 0 3.25-1.46 3.25-3.25v-7.5C20 8.45 "
    "18.54 7 16.75 7H7.25Zm0 1.5h9.5c.97 0 1.75.78 1.75 1.75v7.5c0 .97-.78 1.75-1.75 1.75h-9.5c-.97 0-1.75-.78"
    "-1.75-1.75v-7.5c0-.97.78-1.75 1.75-1.75Z";
constexpr std::string_view lockDotPath = "M12 15.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Z";
constexpr std::string_view lockShacklePath = "M8 7V5a4 4 0 0 1 8 0v2h-1.5V5a2.5 2.5 0 0 0-5 0v2H8Z";
constexpr D2D1_POINT_2F shacklePivot{16.0f, 7.0f};
constexpr float lockCanvasUnits = 24.0f;

constexpr float clamp01(float t) noexcept {
    return std::clamp(t, 0.0f, 1.0f);
}
float easeOutCubic(float t) noexcept {
    const float u = 1.0f - clamp01(t);
    return 1.0f - u * u * u;
}
float easeInCubic(float t) noexcept {
    t = clamp01(t);
    return t * t * t;
}
float easeOutQuad(float t) noexcept {
    const float u = 1.0f - clamp01(t);
    return 1.0f - u * u;
}
float easeInOutCubic(float t) noexcept {
    t = clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

const wchar_t* keyName(const i18n::Strings& strings, LockKey key) noexcept {
    switch (key) {
        case LockKey::CapsLock: return strings.capsLock.wide;
        case LockKey::NumLock: return strings.numLock.wide;
        case LockKey::ScrollLock: return strings.scrollLock.wide;
        case LockKey::Insert: return strings.insertPressed.wide;
    }
    return L"";
}

Color systemAccent() {
    try {
        const winrt::Windows::UI::ViewManagement::UISettings settings;
        const auto accent = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        return {accent.R / 255.0f, accent.G / 255.0f, accent.B / 255.0f, 1.0f};
    } catch (const winrt::hresult_error&) {
        return config::defaultAccentColor;
    }
}

constexpr D2D1_COLOR_F toD2D(const Color& c) noexcept {
    return {c.r, c.g, c.b, c.a};
}

}  // namespace

LockKeyOverlay::LockKeyOverlay(HINSTANCE instance, render::Graphics graphics)
    : m_instance(instance),
      m_graphics(std::move(graphics)),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &LockKeyOverlay::windowProc,
          .hInstance = instance,
          .lpszClassName = overlayClassName,
      }) {}

LockKeyOverlay::~LockKeyOverlay() = default;

Result<std::unique_ptr<LockKeyOverlay>> LockKeyOverlay::create(HINSTANCE instance) {
    Result<render::Graphics> graphics = render::Graphics::create();
    if (!graphics) {
        return graphics.error();
    }
    std::unique_ptr<LockKeyOverlay> overlay{new LockKeyOverlay(instance, std::move(graphics.value()))};
    if (const Result<void> ready = overlay->init(); !ready) {
        return ready.error();
    }
    return overlay;
}

Result<void> LockKeyOverlay::init() {
    if (!m_class.registered()) {
        return Error::fromLastError("RegisterClassEx(ThrenodyLockKeyOverlay)");
    }

    // Click-through, never activated, out of alt-tab, above everything.
    m_hwnd.reset(CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 m_class.name(), L"Threnody lock key", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                 m_instance, this));
    if (!m_hwnd) {
        return Error::fromLastError("CreateWindowEx(ThrenodyLockKeyOverlay)");
    }

    IDWriteFactory2& dwrite = *m_graphics.dwrite;
    HRESULT hr = dwrite.CreateTextFormat(config::fontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, config::lockOverlayFontSizeDip, L"es-ES",
                                         m_textFormat.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateTextFormat(lock overlay)");
    }
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    winrt::com_ptr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(dwrite.CreateEllipsisTrimmingSign(m_textFormat.get(), ellipsis.put()))) {
        const DWRITE_TRIMMING trimming{.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER};
        m_textFormat->SetTrimming(&trimming, ellipsis.get());
    }

    for (const auto& [path, target] : {std::pair{lockBodyPath, &m_lockBody}, std::pair{lockDotPath, &m_lockDot},
                                       std::pair{lockShacklePath, &m_lockShackle}}) {
        Result<winrt::com_ptr<ID2D1PathGeometry>> geometry = render::pathGeometryFromSvg(*m_graphics.d2d, path);
        if (!geometry) {
            return geometry.error();
        }
        *target = std::move(geometry.value());
    }

    m_accent = systemAccent();
    m_indicatorWidth = {config::lockOverlayIndicatorWidthDip, config::lockOverlayIndicatorWidthDip};
    m_indicatorOpacity = {1.0f, 1.0f};
    return {};
}

// Recomputes geometry against the current work area and DPI. Called on every
// show so monitor or scale changes are picked up without restarting.
void LockKeyOverlay::place() {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &m_workArea, 0);
    m_dpi = GetDpiForWindow(m_hwnd.get());
    if (m_dpi == 0) {
        m_dpi = GetDpiForSystem();
    }
    // Default width unless the text is wider; the reference does the same for
    // translations, and the Spanish strings are longer than the English ones.
    m_widthDip = config::lockOverlayWidthDip;
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (SUCCEEDED(m_graphics.dwrite->CreateTextLayout(m_text.c_str(), static_cast<UINT32>(m_text.size()),
                                                      m_textFormat.get(), 4096.0f, config::lockOverlayHeightDip,
                                                      layout.put()))) {
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        const float needed = std::ceil(metrics.widthIncludingTrailingWhitespace) + config::lockOverlayPaddingLeftDip +
                             config::lockOverlayTextLeftMarginDip + config::lockOverlayPaddingRightDip +
                             config::lockOverlayTextSlackDip;
        m_widthDip = std::max(m_widthDip, needed);
    }

    const int width = win32::scaleDip(static_cast<int>(std::ceil(m_widthDip)), m_dpi);
    const int height = win32::scaleDip(static_cast<int>(config::lockOverlayHeightDip), m_dpi);
    m_left = m_workArea.left + (win32::width(m_workArea) - width) / 2;
    m_shownTop = m_workArea.top + win32::scaleDip(config::lockOverlayTopMarginDip, m_dpi);
    m_hiddenTop = m_workArea.top - height - win32::scaleDip(4, m_dpi);
    SetWindowPos(m_hwnd.get(), HWND_TOPMOST, m_left, m_hiddenTop, width, height, SWP_NOACTIVATE);
    if (const Result<void> resized = m_surface.resize(SIZE{.cx = width, .cy = height}); !resized) {
        log::error("{}", resized.error().describe());
    }
}

void LockKeyOverlay::setPhase(Phase phase) {
    m_phase = phase;
    m_phaseStart = GetTickCount64();
    if (phase == Phase::Hidden) {
        KillTimer(m_hwnd.get(), frameTimerId);
        ShowWindow(m_hwnd.get(), SW_HIDE);
    } else {
        SetTimer(m_hwnd.get(), frameTimerId, frameMs, nullptr);
    }
}

void LockKeyOverlay::show(LockKey key, bool on) {
    if (!m_hwnd) {
        return;
    }
    if (key == LockKey::Insert) {
        m_text = m_strings->insertPressed.wide;
        on = true;
    } else {
        m_text = std::wstring{keyName(*m_strings, key)} + L' ' + (on ? m_strings->lockOn.wide : m_strings->lockOff.wide);
    }
    m_accent = systemAccent();

    // Status animation: from wherever the previous state left things.
    const ULONGLONG now = GetTickCount64();
    const float elapsed = static_cast<float>(now - m_statusStart) / static_cast<float>(config::lockOverlayStatusMs);
    m_shackleAngle = {m_shackleAngle.at(easeOutCubic(elapsed)), on ? 0.0f : config::lockOverlayShackleOpenDegrees};
    m_indicatorWidth = {m_indicatorWidth.at(easeOutQuad(elapsed)),
                        on ? config::lockOverlayIndicatorWidthDip : config::lockOverlayIndicatorOffWidthDip};
    m_indicatorOpacity = {m_indicatorOpacity.at(easeOutQuad(elapsed)), on ? 1.0f : config::lockOverlayIndicatorOffOpacity};
    m_statusStart = now;

    switch (m_phase) {
        case Phase::Hidden:
            place();
            setPhase(Phase::Opening);
            break;
        case Phase::Closing:
            place();
            setPhase(Phase::Opening);
            break;
        case Phase::Opening:
            place();
            break;
        case Phase::Shown:
            place();  // The text, and so the width, may have changed.
            m_phaseStart = now;  // Restart the two-second hold.
            break;
    }
    frame();
}

void LockKeyOverlay::frame() {
    const ULONGLONG now = GetTickCount64();
    const float slideT = static_cast<float>(now - m_phaseStart) / static_cast<float>(config::lockOverlaySlideMs);

    int top = m_shownTop;
    float alpha = 1.0f;
    switch (m_phase) {
        case Phase::Hidden:
            return;
        case Phase::Opening: {
            const float e = easeOutCubic(slideT);
            top = m_hiddenTop + static_cast<int>(std::lround((m_shownTop - m_hiddenTop) * e));
            alpha = e;
            if (slideT >= 1.0f) {
                setPhase(Phase::Shown);
            }
            break;
        }
        case Phase::Shown:
            if (now - m_phaseStart >= config::lockOverlayHoldMs) {
                setPhase(Phase::Closing);
            }
            break;
        case Phase::Closing: {
            const float e = easeInCubic(slideT);
            top = m_shownTop + static_cast<int>(std::lround((m_hiddenTop - m_shownTop) * e));
            alpha = 1.0f - e;
            if (slideT >= 1.0f) {
                setPhase(Phase::Hidden);
                return;
            }
            break;
        }
    }

    const float statusT = static_cast<float>(now - m_statusStart) / static_cast<float>(config::lockOverlayStatusMs);
    const float bounce = statusT < 0.1f ? easeOutCubic(statusT / 0.1f)
                                        : 1.0f - easeInOutCubic((statusT - 0.1f) / 0.9f);
    const Result<void> drawn = draw(m_shackleAngle.at(easeOutCubic(statusT)), clamp01(bounce),
                                    m_indicatorWidth.at(easeOutQuad(statusT)), m_indicatorOpacity.at(easeOutQuad(statusT)));
    if (!drawn) {
        log::error("{}", drawn.error().describe());
        setPhase(Phase::Hidden);
        return;
    }

    SetWindowPos(m_hwnd.get(), HWND_TOPMOST, m_left, top, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
    if (const Result<void> presented = m_surface.present(m_hwnd.get(), static_cast<BYTE>(std::lround(alpha * 255.0f)));
        !presented) {
        log::error("{}", presented.error().describe());
        setPhase(Phase::Hidden);
        return;
    }
    if (!IsWindowVisible(m_hwnd.get())) {
        ShowWindow(m_hwnd.get(), SW_SHOWNOACTIVATE);
    }
}

Result<void> LockKeyOverlay::ensureTarget() {
    if (!m_target) {
        const D2D1_RENDER_TARGET_PROPERTIES properties{
            .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
            .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
            .dpiX = static_cast<float>(m_dpi),
            .dpiY = static_cast<float>(m_dpi),
            .usage = D2D1_RENDER_TARGET_USAGE_NONE,
            .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
        };
        HRESULT hr = m_graphics.d2d->CreateDCRenderTarget(&properties, m_target.put());
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "CreateDCRenderTarget(lock overlay)");
        }
        m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        hr = m_target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, m_brush.put());
        if (FAILED(hr)) {
            m_target = nullptr;
            return Error::fromHResult(hr, "CreateSolidColorBrush(lock overlay)");
        }
        m_boundDc = nullptr;
    }
    m_target->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    if (m_boundDc != m_surface.dc()) {
        const SIZE size = m_surface.size();
        const RECT bounds{0, 0, size.cx, size.cy};
        const HRESULT hr = m_target->BindDC(m_surface.dc(), &bounds);
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "BindDC(lock overlay)");
        }
        m_boundDc = m_surface.dc();
    }
    return {};
}

Result<void> LockKeyOverlay::draw(float shackleAngle, float shackleBounce, float indicatorWidth, float indicatorOpacity) {
    if (const Result<void> ready = ensureTarget(); !ready) {
        return ready;
    }
    using namespace config;
    const float width = m_widthDip;
    const float height = lockOverlayHeightDip;

    ID2D1DCRenderTarget& target = *m_target;
    target.BeginDraw();
    target.SetTransform(D2D1::Matrix3x2F::Identity());
    target.Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    // Translucent dark panel with the Windows 11 corner radius.
    const D2D1_ROUNDED_RECT panel{{0.0f, 0.0f, width, height}, lockOverlayCornerRadiusDip, lockOverlayCornerRadiusDip};
    m_brush->SetColor(toD2D(lockOverlayBackgroundColor));
    target.FillRoundedRectangle(panel, m_brush.get());
    const D2D1_ROUNDED_RECT border{{0.5f, 0.5f, width - 0.5f, height - 0.5f}, lockOverlayCornerRadiusDip,
                                   lockOverlayCornerRadiusDip};
    m_brush->SetColor(toD2D(lockOverlayBorderColor));
    target.DrawRoundedRectangle(border, m_brush.get(), 1.0f);

    // Padlock: 22 x 22 at the left, vertically centred, nudged down one pixel
    // like the reference layout.
    const float iconScale = lockOverlayIconSizeDip / lockCanvasUnits;
    const float iconLeft = lockOverlayPaddingLeftDip;
    const float iconTop = (height - lockOverlayIconSizeDip) / 2.0f + 1.0f;
    const D2D1::Matrix3x2F iconTransform =
        D2D1::Matrix3x2F::Scale(iconScale, iconScale) * D2D1::Matrix3x2F::Translation(iconLeft, iconTop);
    m_brush->SetColor(toD2D(lockOverlayForegroundColor));
    target.SetTransform(iconTransform);
    target.FillGeometry(m_lockBody.get(), m_brush.get());
    target.FillGeometry(m_lockDot.get(), m_brush.get());
    target.SetTransform(D2D1::Matrix3x2F::Rotation(shackleAngle, shacklePivot) *
                        D2D1::Matrix3x2F::Translation(0.0f, shackleBounce) * iconTransform);
    target.FillGeometry(m_lockShackle.get(), m_brush.get());
    target.SetTransform(D2D1::Matrix3x2F::Identity());

    // Status text, centred in the space right of the icon, lifted a little so
    // the indicator bar has room.
    const D2D1_RECT_F textRect{lockOverlayPaddingLeftDip + lockOverlayTextLeftMarginDip, 0.0f,
                               width - lockOverlayPaddingRightDip, height - lockOverlayTextBottomMarginDip};
    m_brush->SetColor(toD2D(lockOverlayForegroundColor));
    target.DrawTextW(m_text.c_str(), static_cast<UINT32>(m_text.size()), m_textFormat.get(), textRect, m_brush.get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Accent indicator along the bottom.
    const float indicatorLeft = (width - indicatorWidth) / 2.0f;
    const float indicatorBottom = height - lockOverlayPaddingBottomDip;
    const D2D1_ROUNDED_RECT indicator{
        {indicatorLeft, indicatorBottom - lockOverlayIndicatorHeightDip, indicatorLeft + indicatorWidth, indicatorBottom},
        lockOverlayIndicatorHeightDip / 2.0f, lockOverlayIndicatorHeightDip / 2.0f};
    m_brush->SetColor(toD2D(m_accent.withAlpha(indicatorOpacity)));
    target.FillRoundedRectangle(indicator, m_brush.get());

    const HRESULT hr = target.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        m_brush = nullptr;
        m_target = nullptr;
        return Error::fromHResult(hr, "lock overlay target lost");
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "EndDraw(lock overlay)");
    }
    return {};
}

LRESULT CALLBACK LockKeyOverlay::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<LockKeyOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT LockKeyOverlay::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
