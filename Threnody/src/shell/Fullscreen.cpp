#include "shell/Fullscreen.h"

#include <Windows.h>

#include <array>
#include <string_view>

namespace threnody::shell {

bool isFullscreenApplicationInFront() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || foreground == GetDesktopWindow() || foreground == GetShellWindow()) {
        return false;
    }

    std::array<wchar_t, 64> buffer{};
    const int length = GetClassNameW(foreground, buffer.data(), static_cast<int>(buffer.size()));
    const std::wstring_view className{buffer.data(), static_cast<std::size_t>(length)};
    if (className == L"WorkerW" || className == L"Progman" || className == L"Shell_TrayWnd") {
        return false;
    }

    RECT window{};
    if (!GetWindowRect(foreground, &window)) {
        return false;
    }
    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }
    return window.left <= info.rcMonitor.left && window.top <= info.rcMonitor.top &&
           window.right >= info.rcMonitor.right && window.bottom >= info.rcMonitor.bottom;
}

}  // namespace threnody::shell
