#pragma once

#include "render/LayeredSurface.h"
#include "taskbar/RegistryWatcher.h"
#include "taskbar/Taskbar.h"
#include "taskbar/WidgetWindow.h"
#include "util/Win32.h"

#include <Windows.h>

#include <memory>
#include <optional>

namespace threnody {

// Owns the message loop and wires the taskbar pieces together: a hidden
// top-level window receives broadcasts (TaskbarCreated), timer ticks and
// registry-change notifications, and reacts by (re)embedding or moving the
// widget.
class Application {
public:
    explicit Application(HINSTANCE instance);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs until the message window is closed. Returns the exit code.
    [[nodiscard]] int run();

private:
    static LRESULT CALLBACK messageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void syncWithTaskbar();
    void repaintWidget();

    HINSTANCE m_instance{};
    win32::WindowClass m_messageClass;
    win32::unique_hwnd m_messageWindow;
    UINT m_taskbarCreatedMessage{};

    taskbar::WidgetWindow m_widget;
    render::LayeredSurface m_surface;
    std::unique_ptr<taskbar::RegistryWatcher> m_alignmentWatcher;
    std::optional<taskbar::Layout> m_layout;
    RECT m_widgetRect{};
};

}  // namespace threnody
