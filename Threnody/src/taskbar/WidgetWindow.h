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
    // Edges of a floating widget grabbed for resizing; combined for corners.
    enum Edge : UINT { EdgeLeft = 1, EdgeTop = 2, EdgeRight = 4, EdgeBottom = 8 };

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
    // Fired when the button goes down on an edge of the floating widget.
    void onResizeStart(std::function<void(UINT edges)> handler) { m_onResizeStart = std::move(handler); }
    // Asked when the button goes down elsewhere: returning true makes the
    // press a scrub instead of a click or a drag, reported to onScrub on
    // every move and once more, with `done`, when the button comes up.
    void onPress(std::function<bool(POINT position)> handler) { m_onPress = std::move(handler); }
    void onScrub(std::function<void(POINT position, bool done)> handler) { m_onScrub = std::move(handler); }
    // Fired when the right button comes up over the widget (not mid-drag).
    void onContextMenu(ClickHandler handler) { m_onContextMenu = std::move(handler); }

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd.get(); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] UINT edgesAt(POINT client) const noexcept;
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance{};
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    ClickHandler m_onClick;
    ClickHandler m_onPointerMove;
    std::function<void()> m_onPointerLeave;
    ClickHandler m_onDragStart;
    std::function<void(UINT edges)> m_onResizeStart;
    std::function<bool(POINT position)> m_onPress;
    std::function<void(POINT position, bool done)> m_onScrub;
    ClickHandler m_onContextMenu;
    bool m_scrubbing{false};
    POINT m_lastScrub{};
    bool m_hovering{false};
    bool m_floating{false};
    std::optional<POINT> m_press;  // Button down, no drag yet.
    bool m_dragged{false};         // The current press became a drag.
};

}  // namespace threnody::taskbar
