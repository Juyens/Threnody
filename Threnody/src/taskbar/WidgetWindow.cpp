#include "taskbar/WidgetWindow.h"

#include "util/Log.h"

#include <windowsx.h>

#include <cstdlib>

namespace threnody::taskbar {
namespace {

constexpr wchar_t widgetClassName[] = L"ThrenodyWidget";
constexpr int dragThresholdDip = 6;  // Past this, a press is a drag rather than a click.

}  // namespace

WidgetWindow::WidgetWindow(HINSTANCE instance)
    : m_instance(instance),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &WidgetWindow::windowProc,
          .hInstance = instance,
          .hCursor = LoadCursorW(nullptr, IDC_ARROW),
          .lpszClassName = widgetClassName,
      }) {
    if (!m_class.registered()) {
        log::error("{}", Error::fromLastError("RegisterClassEx(ThrenodyWidget)").describe());
    }
}

WidgetWindow::~WidgetWindow() = default;

Result<void> WidgetWindow::embed(HWND taskbar, const RECT& rect) {
    m_hwnd.reset();
    m_floating = false;

    // Created as a top-level popup first, then converted to a child; this is
    // the sequence that has been verified to work against the taskbar.
    //
    // WS_EX_LAYERED is essential, not cosmetic: the Windows 11 taskbar draws
    // its XAML content through a composition tree layered above its GDI
    // surface, so an ordinary child window paints underneath and never shows.
    // A per-pixel layered window gets its own DWM surface composited on top.
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, m_class.name(), L"Threnody",
                                WS_POPUP, rect.left, rect.top, win32::width(rect), win32::height(rect), nullptr,
                                nullptr, m_instance, this);
    if (hwnd == nullptr) {
        return Error::fromLastError("CreateWindowEx(ThrenodyWidget)");
    }
    m_hwnd.reset(hwnd);

    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    SetWindowLongW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);

    SetLastError(ERROR_SUCCESS);
    if (SetParent(hwnd, taskbar) == nullptr && GetLastError() != ERROR_SUCCESS) {
        m_hwnd.reset();
        return Error::fromLastError("SetParent(widget, Shell_TrayWnd)");
    }

    move(rect);
    return {};
}

Result<void> WidgetWindow::makeFloating(const RECT& rect) {
    m_hwnd.reset();
    m_hovering = false;
    m_press.reset();
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, m_class.name(),
                                L"Threnody", WS_POPUP, rect.left, rect.top, win32::width(rect), win32::height(rect),
                                nullptr, nullptr, m_instance, this);
    if (hwnd == nullptr) {
        return Error::fromLastError("CreateWindowEx(ThrenodyWidget, floating)");
    }
    m_hwnd.reset(hwnd);
    m_floating = true;
    return {};
}

bool WidgetWindow::isEmbeddedIn(HWND taskbar) const noexcept {
    const HWND hwnd = m_hwnd.get();
    return hwnd != nullptr && IsWindow(hwnd) && GetParent(hwnd) == taskbar;
}

void WidgetWindow::move(const RECT& rect) const noexcept {
    if (m_hwnd) {
        // A floating widget keeps its place in the topmost band.
        SetWindowPos(m_hwnd.get(), m_floating ? HWND_TOPMOST : HWND_TOP, rect.left, rect.top, win32::width(rect),
                     win32::height(rect), SWP_NOACTIVATE);
    }
}

void WidgetWindow::show() const noexcept {
    if (m_hwnd && !IsWindowVisible(m_hwnd.get())) {
        ShowWindow(m_hwnd.get(), SW_SHOWNOACTIVATE);
    }
}

LRESULT CALLBACK WidgetWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT WidgetWindow::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            // Content comes from UpdateLayeredWindow; just validate.
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE:
            if (!m_hovering) {
                m_hovering = true;
                TRACKMOUSEEVENT track{.cbSize = sizeof(TRACKMOUSEEVENT), .dwFlags = TME_LEAVE, .hwndTrack = hwnd};
                TrackMouseEvent(&track);
            }
            if (m_press) {
                const POINT at{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
                const UINT dpi = GetDpiForWindow(hwnd);
                const int threshold = MulDiv(dragThresholdDip, dpi == 0 ? 96 : static_cast<int>(dpi), 96);
                if (std::abs(at.x - m_press->x) >= threshold || std::abs(at.y - m_press->y) >= threshold) {
                    const POINT press = *m_press;
                    m_press.reset();
                    m_dragged = true;
                    ReleaseCapture();
                    if (m_onDragStart) {
                        m_onDragStart(press);
                    }
                    return 0;
                }
            }
            if (m_onPointerMove) {
                m_onPointerMove(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)});
            }
            return 0;

        case WM_MOUSELEAVE:
            m_hovering = false;
            if (m_onPointerLeave) {
                m_onPointerLeave();
            }
            return 0;

        case WM_LBUTTONDOWN:
            m_press = POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
            m_dragged = false;
            SetCapture(hwnd);  // Keeps the moves coming if the pointer leaves quickly.
            return 0;

        case WM_LBUTTONUP: {
            // An up without a down (posted by a test, or the down went to the
            // taskbar) still counts as a click; an up ending a drag does not.
            const bool click = !m_dragged;
            m_press.reset();
            m_dragged = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            if (click && m_onClick) {
                m_onClick(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)});
            }
            return 0;
        }

        case WM_CAPTURECHANGED:
            m_press.reset();
            return 0;

        case WM_NCDESTROY:
            // Either we destroyed it, or the taskbar took it down with itself.
            // In both cases the handle is dead; never DestroyWindow it again.
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (m_hwnd.get() == hwnd) {
                m_hwnd.release();
                log::warn("widget window destroyed from outside; will re-embed");
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace threnody::taskbar
