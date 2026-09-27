#include "taskbar/DockPreview.h"

#include "Config.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>

namespace threnody::taskbar {
namespace {

constexpr wchar_t previewClassName[] = L"ThrenodyDockPreview";
constexpr UINT_PTR frameTimerId = 1;
constexpr unsigned frameMs = 16;

constexpr D2D1_COLOR_F toD2D(const Color& c) noexcept {
    return {c.r, c.g, c.b, c.a};
}

}  // namespace

DockPreview::DockPreview(HINSTANCE instance, render::Graphics graphics)
    : m_instance(instance),
      m_graphics(std::move(graphics)),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &DockPreview::windowProc,
          .hInstance = instance,
          .lpszClassName = previewClassName,
      }) {}

DockPreview::~DockPreview() = default;

Result<std::unique_ptr<DockPreview>> DockPreview::create(HINSTANCE instance) {
    Result<render::Graphics> graphics = render::Graphics::create();
    if (!graphics) {
        return graphics.error();
    }
    std::unique_ptr<DockPreview> preview{new DockPreview(instance, std::move(graphics.value()))};
    if (const Result<void> ready = preview->init(); !ready) {
        return ready.error();
    }
    return preview;
}

Result<void> DockPreview::init() {
    if (!m_class.registered()) {
        return Error::fromLastError("RegisterClassEx(ThrenodyDockPreview)");
    }
    m_hwnd.reset(CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 m_class.name(), L"Threnody dock preview", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                 m_instance, this));
    if (!m_hwnd) {
        return Error::fromLastError("CreateWindowEx(ThrenodyDockPreview)");
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties{
        .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
        .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
        .usage = D2D1_RENDER_TARGET_USAGE_NONE,
        .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
    };
    HRESULT hr = m_graphics.d2d->CreateDCRenderTarget(&properties, m_target.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateDCRenderTarget(dock preview)");
    }
    hr = m_target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, m_brush.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateSolidColorBrush(dock preview)");
    }
    return {};
}

void DockPreview::show(const RECT& rect, UINT dpi) {
    if (!m_hwnd) {
        return;
    }
    const bool visible = IsWindowVisible(m_hwnd.get()) != FALSE;
    if (visible && win32::sameRect(rect, m_rect) && dpi == m_dpi) {
        return;
    }
    m_rect = rect;
    m_dpi = dpi == 0 ? 96 : dpi;
    if (const Result<void> resized = m_surface.resize(SIZE{.cx = win32::width(rect), .cy = win32::height(rect)});
        !resized) {
        log::error("{}", resized.error().describe());
        return;
    }
    if (const Result<void> drawn = draw(); !drawn) {
        log::error("{}", drawn.error().describe());
        return;
    }
    SetWindowPos(m_hwnd.get(), HWND_TOPMOST, rect.left, rect.top, win32::width(rect), win32::height(rect),
                 SWP_NOACTIVATE);
    if (!visible) {
        m_shownTick = GetTickCount64();
        frame();
        ShowWindow(m_hwnd.get(), SW_SHOWNOACTIVATE);
        SetTimer(m_hwnd.get(), frameTimerId, frameMs, nullptr);
    } else {
        frame();
    }
}

void DockPreview::hide() {
    if (m_hwnd && IsWindowVisible(m_hwnd.get())) {
        KillTimer(m_hwnd.get(), frameTimerId);
        ShowWindow(m_hwnd.get(), SW_HIDE);
    }
}

// Fades in; the content itself is drawn once per show.
void DockPreview::frame() {
    const float t = static_cast<float>(GetTickCount64() - m_shownTick) / config::dockPreviewFadeMs;
    const float alpha = std::clamp(t, 0.0f, 1.0f);
    if (const Result<void> presented =
            m_surface.present(m_hwnd.get(), static_cast<BYTE>(std::lround(alpha * 255.0f)));
        !presented) {
        log::error("{}", presented.error().describe());
    }
    if (alpha >= 1.0f) {
        KillTimer(m_hwnd.get(), frameTimerId);
    }
}

Result<void> DockPreview::draw() {
    m_target->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    const RECT bounds{0, 0, win32::width(m_rect), win32::height(m_rect)};
    if (const HRESULT hr = m_target->BindDC(m_surface.dc(), &bounds); FAILED(hr)) {
        return Error::fromHResult(hr, "BindDC(dock preview)");
    }
    const float width = static_cast<float>(bounds.right) * 96.0f / static_cast<float>(m_dpi);
    const float height = static_cast<float>(bounds.bottom) * 96.0f / static_cast<float>(m_dpi);
    const float radius = config::backgroundCornerRadiusDip;
    const float stroke = config::dockPreviewStrokeDip;

    m_target->BeginDraw();
    m_target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
    m_brush->SetColor(toD2D(config::dockPreviewFillColor));
    m_target->FillRoundedRectangle({{0.0f, 0.0f, width, height}, radius, radius}, m_brush.get());
    m_brush->SetColor(toD2D(config::dockPreviewStrokeColor));
    const float inset = stroke / 2.0f;
    m_target->DrawRoundedRectangle({{inset, inset, width - inset, height - inset}, radius, radius}, m_brush.get(),
                                   stroke);
    const HRESULT hr = m_target->EndDraw();
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "EndDraw(dock preview)");
    }
    return {};
}

LRESULT CALLBACK DockPreview::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<DockPreview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT DockPreview::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
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

}  // namespace threnody::taskbar
