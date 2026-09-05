#pragma once

#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

namespace threnody::taskbar {

// The widget's HWND, living as a per-pixel layered WS_CHILD of Shell_TrayWnd.
// Its content is pushed with UpdateLayeredWindow (see render::LayeredSurface),
// so WM_PAINT does nothing here.
//
// The taskbar destroys its children when explorer restarts, so this window
// can die underneath us; `embed` recreates it and `isEmbeddedIn` says whether
// it is still attached.
class WidgetWindow {
public:
    explicit WidgetWindow(HINSTANCE instance);
    ~WidgetWindow();

    WidgetWindow(const WidgetWindow&) = delete;
    WidgetWindow& operator=(const WidgetWindow&) = delete;

    // Creates the window if needed, reparents it into `taskbar` and places it
    // at `rect` (taskbar client coordinates). Shown once content is presented.
    [[nodiscard]] Result<void> embed(HWND taskbar, const RECT& rect);
    [[nodiscard]] bool isEmbeddedIn(HWND taskbar) const noexcept;

    void move(const RECT& rect) const noexcept;
    void show() const noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd.get(); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance{};
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
};

}  // namespace threnody::taskbar
