#include "overlay/VolumeFlyout.h"

#include "Config.h"
#include "render/Icons.h"
#include "render/SvgPath.h"
#include "util/Log.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace threnody::overlay {
namespace {

constexpr wchar_t flyoutClassName[] = L"ThrenodyVolumeFlyout";
constexpr UINT_PTR frameTimerId = 1;
constexpr unsigned frameMs = 16;
constexpr unsigned watchMs = 100;         // After the fade: checks for clicks elsewhere.
constexpr ULONGLONG reopenGuardMs = 400;  // See justClosed().

constexpr D2D1_COLOR_F toD2D(const Color& c) noexcept {
    return {c.r, c.g, c.b, c.a};
}

}  // namespace

VolumeFlyout::VolumeFlyout(HINSTANCE instance, render::Graphics graphics, Callbacks callbacks)
    : m_instance(instance),
      m_graphics(std::move(graphics)),
      m_callbacks(std::move(callbacks)),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &VolumeFlyout::windowProc,
          .hInstance = instance,
          .hCursor = LoadCursorW(nullptr, IDC_ARROW),
          .lpszClassName = flyoutClassName,
      }) {}

VolumeFlyout::~VolumeFlyout() = default;

Result<std::unique_ptr<VolumeFlyout>> VolumeFlyout::create(HINSTANCE instance, Callbacks callbacks) {
    Result<render::Graphics> graphics = render::Graphics::create();
    if (!graphics) {
        return graphics.error();
    }
    std::unique_ptr<VolumeFlyout> flyout{new VolumeFlyout(instance, std::move(graphics.value()), std::move(callbacks))};
    if (const Result<void> ready = flyout->init(); !ready) {
        return ready.error();
    }
    return flyout;
}

Result<void> VolumeFlyout::init() {
    if (!m_class.registered()) {
        return Error::fromLastError("RegisterClassEx(ThrenodyVolumeFlyout)");
    }
    // Activatable, so it gets the wheel and keys and learns when the user
    // clicks elsewhere; out of alt-tab and above the taskbar.
    m_hwnd.reset(CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, m_class.name(), L"Threnody volume",
                                 WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, m_instance, this));
    if (!m_hwnd) {
        return Error::fromLastError("CreateWindowEx(ThrenodyVolumeFlyout)");
    }

    HRESULT hr = m_graphics.dwrite->CreateTextFormat(config::fontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     config::volumeFlyoutFontSizeDip, L"", m_textFormat.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateTextFormat(volume flyout)");
    }
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const std::array paths{render::icons::speakerMute, render::icons::speaker0, render::icons::speaker1,
                           render::icons::speaker2};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        Result<winrt::com_ptr<ID2D1PathGeometry>> geometry = render::pathGeometryFromSvg(*m_graphics.d2d, paths[i]);
        if (!geometry) {
            return geometry.error();
        }
        m_speakerIcons[i] = std::move(geometry.value());
    }
    return {};
}

void VolumeFlyout::open(const RECT& anchor, UINT dpi, float level, bool muted) {
    if (!m_hwnd) {
        return;
    }
    m_dpi = dpi == 0 ? 96 : dpi;
    m_level = std::clamp(level, 0.0f, 1.0f);
    m_muted = muted;
    m_dragging = false;

    const int width = win32::scaleDip(static_cast<int>(config::volumeFlyoutWidthDip), m_dpi);
    const int height = win32::scaleDip(static_cast<int>(config::volumeFlyoutHeightDip), m_dpi);
    const int gap = win32::scaleDip(static_cast<int>(config::volumeFlyoutGapDip), m_dpi);

    MONITORINFO monitor{.cbSize = sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& screen = monitor.rcMonitor;
    const RECT& work = monitor.rcWork;

    const int centreX = (anchor.left + anchor.right) / 2;
    const int left = std::clamp(centreX - width / 2, static_cast<int>(work.left), static_cast<int>(work.right) - width);
    const bool taskbarAtTop = (anchor.top + anchor.bottom) / 2 < (screen.top + screen.bottom) / 2;
    // Measured from the taskbar's edge (the work area's), not the button's.
    const int top = taskbarAtTop ? std::max<int>(anchor.bottom, work.top) + gap
                                 : std::min<int>(anchor.top, work.bottom) - gap - height;

    if (const Result<void> resized = m_surface.resize(SIZE{.cx = width, .cy = height}); !resized) {
        log::error("{}", resized.error().describe());
        return;
    }
    SetWindowPos(m_hwnd.get(), HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);

    m_alpha = 0;
    m_openedTick = GetTickCount64();
    redraw();
    ShowWindow(m_hwnd.get(), SW_SHOW);
    SetForegroundWindow(m_hwnd.get());
    SetFocus(m_hwnd.get());
    SetTimer(m_hwnd.get(), frameTimerId, frameMs, nullptr);
}

void VolumeFlyout::close() {
    if (!visible()) {
        return;
    }
    KillTimer(m_hwnd.get(), frameTimerId);
    if (m_dragging) {
        ReleaseCapture();
    }
    m_dragging = false;
    m_hovering = false;
    ShowWindow(m_hwnd.get(), SW_HIDE);
    m_closedTick = GetTickCount64();
}

void VolumeFlyout::setState(float level, bool muted) {
    if (m_dragging) {
        return;  // The pointer owns the level until it lets go.
    }
    level = std::clamp(level, 0.0f, 1.0f);
    if (level == m_level && muted == m_muted) {
        return;
    }
    m_level = level;
    m_muted = muted;
    if (visible()) {
        redraw();
    }
}

bool VolumeFlyout::visible() const noexcept {
    return m_hwnd && IsWindowVisible(m_hwnd.get());
}

bool VolumeFlyout::justClosed() const noexcept {
    return m_closedTick != 0 && GetTickCount64() - m_closedTick < reopenGuardMs;
}

// Fade in; afterwards a slow watch closes the flyout on a click elsewhere in
// case it never got activation (Windows may refuse the foreground switch).
void VolumeFlyout::frame() {
    if (m_alpha < 255) {
        const float t = static_cast<float>(GetTickCount64() - m_openedTick) / config::volumeFlyoutFadeMs;
        m_alpha = static_cast<BYTE>(std::lround(std::clamp(t, 0.0f, 1.0f) * 255.0f));
        if (const Result<void> presented = m_surface.present(m_hwnd.get(), m_alpha); !presented) {
            log::error("{}", presented.error().describe());
        }
        if (m_alpha == 255) {
            SetTimer(m_hwnd.get(), frameTimerId, watchMs, nullptr);
        }
        return;
    }
    if (GetForegroundWindow() == m_hwnd.get() || m_dragging) {
        return;
    }
    const bool pressed = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
    POINT cursor{};
    GetCursorPos(&cursor);
    RECT bounds{};
    GetWindowRect(m_hwnd.get(), &bounds);
    if (pressed && !PtInRect(&bounds, cursor)) {
        close();
    }
}

float VolumeFlyout::toDip(int px) const noexcept {
    return static_cast<float>(px) * 96.0f / static_cast<float>(m_dpi);
}

float VolumeFlyout::trackLeft() const noexcept {
    return config::volumeFlyoutIconZoneDip + config::volumeFlyoutThumbRadiusDip;
}

float VolumeFlyout::trackRight() const noexcept {
    return config::volumeFlyoutWidthDip - config::volumeFlyoutValueZoneDip - config::volumeFlyoutThumbRadiusDip;
}

void VolumeFlyout::setFromPointer(int xPx) {
    const float level = std::clamp((toDip(xPx) - trackLeft()) / (trackRight() - trackLeft()), 0.0f, 1.0f);
    m_level = level;
    m_muted = false;  // Moving the slider unmutes, as in Spotify.
    redraw();
    if (m_callbacks.onLevel) {
        m_callbacks.onLevel(level);
    }
}

void VolumeFlyout::step(float delta) {
    m_level = std::clamp(m_level + delta, 0.0f, 1.0f);
    m_muted = false;
    redraw();
    if (m_callbacks.onLevel) {
        m_callbacks.onLevel(m_level);
    }
}

void VolumeFlyout::redraw() {
    if (const Result<void> drawn = draw(); !drawn) {
        log::error("{}", drawn.error().describe());
        return;
    }
    if (const Result<void> presented = m_surface.present(m_hwnd.get(), m_alpha); !presented) {
        log::error("{}", presented.error().describe());
    }
}

Result<void> VolumeFlyout::ensureTarget() {
    if (!m_target) {
        const D2D1_RENDER_TARGET_PROPERTIES properties{
            .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
            .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
            .usage = D2D1_RENDER_TARGET_USAGE_NONE,
            .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
        };
        HRESULT hr = m_graphics.d2d->CreateDCRenderTarget(&properties, m_target.put());
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "CreateDCRenderTarget(volume flyout)");
        }
        m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        hr = m_target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, m_brush.put());
        if (FAILED(hr)) {
            m_target = nullptr;
            return Error::fromHResult(hr, "CreateSolidColorBrush(volume flyout)");
        }
        m_boundDc = nullptr;
    }
    m_target->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    if (m_boundDc != m_surface.dc()) {
        const SIZE size = m_surface.size();
        const RECT bounds{0, 0, size.cx, size.cy};
        const HRESULT hr = m_target->BindDC(m_surface.dc(), &bounds);
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "BindDC(volume flyout)");
        }
        m_boundDc = m_surface.dc();
    }
    return {};
}

Result<void> VolumeFlyout::draw() {
    if (const Result<void> ready = ensureTarget(); !ready) {
        return ready;
    }
    using namespace config;
    const float width = volumeFlyoutWidthDip;
    const float height = volumeFlyoutHeightDip;
    ID2D1DCRenderTarget& target = *m_target;
    const auto fill = [&](const Color& color) { m_brush->SetColor(toD2D(color)); };

    target.BeginDraw();
    target.SetTransform(D2D1::Matrix3x2F::Identity());
    target.Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    fill(volumeFlyoutBackgroundColor);
    target.FillRoundedRectangle({{0.0f, 0.0f, width, height}, volumeFlyoutCornerRadiusDip, volumeFlyoutCornerRadiusDip},
                                m_brush.get());
    fill(volumeFlyoutBorderColor);
    target.DrawRoundedRectangle(
        {{0.5f, 0.5f, width - 0.5f, height - 0.5f}, volumeFlyoutCornerRadiusDip, volumeFlyoutCornerRadiusDip},
        m_brush.get(), 1.0f);

    // Mute button: the same speaker icons as the widget.
    const float shown = m_muted ? 0.0f : m_level;
    const std::size_t speaker = shown <= 0.0f ? 0 : shown < 0.34f ? 1 : shown < 0.67f ? 2 : 3;
    const float iconScale = volumeFlyoutIconSizeDip / render::icons::canvasUnits;
    const float iconLeft = (volumeFlyoutIconZoneDip - volumeFlyoutIconSizeDip) / 2.0f + 4.0f;
    const float iconTop = (height - volumeFlyoutIconSizeDip) / 2.0f;
    fill(controlColor);
    target.SetTransform(D2D1::Matrix3x2F::Scale(iconScale, iconScale) *
                        D2D1::Matrix3x2F::Translation(iconLeft, iconTop));
    target.FillGeometry(m_speakerIcons[speaker].get(), m_brush.get());
    target.SetTransform(D2D1::Matrix3x2F::Identity());

    // Track, filled up to the level; green while the pointer is on it.
    const float left = trackLeft();
    const float right = trackRight();
    const float centreY = height / 2.0f;
    const float half = volumeFlyoutTrackHeightDip / 2.0f;
    const float x = left + (right - left) * shown;
    fill(volumeFlyoutTrackColor);
    target.FillRoundedRectangle({{left, centreY - half, right, centreY + half}, half, half}, m_brush.get());
    fill(m_hovering || m_dragging ? controlActiveColor : volumeFlyoutFillColor);
    target.FillRoundedRectangle({{left, centreY - half, x, centreY + half}, half, half}, m_brush.get());
    fill(volumeFlyoutFillColor);
    target.FillEllipse(D2D1::Ellipse({x, centreY}, volumeFlyoutThumbRadiusDip, volumeFlyoutThumbRadiusDip),
                       m_brush.get());

    const std::wstring value = std::format(L"{}", static_cast<int>(std::lround(shown * 100.0f)));
    fill(artistColor);
    target.DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), m_textFormat.get(),
                     {width - volumeFlyoutValueZoneDip, 0.0f, width - 14.0f, height}, m_brush.get());

    const HRESULT hr = target.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        m_brush = nullptr;
        m_target = nullptr;
        return Error::fromHResult(hr, "volume flyout target lost");
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "EndDraw(volume flyout)");
    }
    return {};
}

LRESULT CALLBACK VolumeFlyout::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<VolumeFlyout*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT VolumeFlyout::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == frameTimerId) {
                frame();
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                close();
            }
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_LBUTTONDOWN: {
            const float x = toDip(GET_X_LPARAM(lParam));
            if (x < config::volumeFlyoutIconZoneDip) {
                if (m_callbacks.onToggleMute) {
                    m_callbacks.onToggleMute();
                }
                return 0;
            }
            if (x >= trackLeft() - config::volumeFlyoutThumbRadiusDip &&
                x <= trackRight() + config::volumeFlyoutThumbRadiusDip) {
                m_dragging = true;
                SetCapture(hwnd);
                setFromPointer(GET_X_LPARAM(lParam));
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (m_dragging) {
                setFromPointer(GET_X_LPARAM(lParam));
                return 0;
            }
            const float x = toDip(GET_X_LPARAM(lParam));
            const bool over = x >= trackLeft() - config::volumeFlyoutThumbRadiusDip &&
                              x <= trackRight() + config::volumeFlyoutThumbRadiusDip;
            if (over != m_hovering) {
                m_hovering = over;
                TRACKMOUSEEVENT track{.cbSize = sizeof(TRACKMOUSEEVENT), .dwFlags = TME_LEAVE, .hwndTrack = hwnd};
                TrackMouseEvent(&track);
                redraw();
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (m_hovering) {
                m_hovering = false;
                redraw();
            }
            return 0;

        case WM_LBUTTONUP:
            if (m_dragging) {
                ReleaseCapture();
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (m_dragging) {
                m_dragging = false;
                redraw();
            }
            return 0;

        case WM_MOUSEWHEEL:
            step(config::volumeStep * static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
            return 0;

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE: close(); return 0;
                case VK_LEFT:
                case VK_DOWN: step(-config::volumeStep); return 0;
                case VK_RIGHT:
                case VK_UP: step(config::volumeStep); return 0;
            }
            break;

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
