#pragma once

#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <functional>
#include <optional>

namespace threnody::taskbar {

// The widget's HWND, living as a per-pixel layered WS_CHILD of Shell_TrayWnd,
// or, once dragged out, as a topmost layered popup floating on the desktop.
// Its content is pushed with UpdateLayeredWindow (see render::LayeredSurface),
// so WM_PAINT does nothing here.
//
// The taskbar destroys its children when explorer restarts, so this window
// can die underneath us; `embed` recreates it and `isEmbeddedIn` says whether
// it is still attached.
class WidgetWindow {
public:
    // Client-area position in physical pixels.
    using ClickHandler = std::function<void(POINT position)>;

    explicit WidgetWindow(HINSTANCE instance);
    ~WidgetWindow();

    WidgetWindow(const WidgetWindow&) = delete;
    WidgetWindow& operator=(const WidgetWindow&) = delete;

    // Creates the window if needed, reparents it into `taskbar` and places it
    // at `rect` (taskbar client coordinates). Shown once content is presented.
    [[nodiscard]] Result<void> embed(HWND taskbar, const RECT& rect);
    [[nodiscard]] bool isEmbeddedIn(HWND taskbar) const noexcept;

    // Recreates the window as a floating popup at `rect` (screen pixels).
    [[nodiscard]] Result<void> makeFloating(const RECT& rect);
    [[nodiscard]] bool isFloating() const noexcept { return m_hwnd && m_floating; }

    void move(const RECT& rect) const noexcept;
    void show() const noexcept;

    void onClick(ClickHandler handler) { m_onClick = std::move(handler); }
    // Fired on every pointer move over the widget, and once when it leaves.
    void onPointerMove(ClickHandler handler) { m_onPointerMove = std::move(handler); }
    void onPointerLeave(std::function<void()> handler) { m_onPointerLeave = std::move(handler); }
    // Fired once the pointer moves past the drag threshold with the button
    // held; `position` is where the button went down. The click that would
    // have followed is suppressed.
    void onDragStart(ClickHandler handler) { m_onDragStart = std::move(handler); }

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd.get(); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance{};
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    ClickHandler m_onClick;
    ClickHandler m_onPointerMove;
    std::function<void()> m_onPointerLeave;
    ClickHandler m_onDragStart;
    bool m_hovering{false};
    bool m_floating{false};
    std::optional<POINT> m_press;  // Button down, no drag yet.
    bool m_dragged{false};         // The current press became a drag.
};

}  // namespace threnody::taskbar
