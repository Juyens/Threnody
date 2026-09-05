#include "shell/SpotifyWindow.h"

#include "shell/SpotifyProcess.h"
#include "taskbar/Taskbar.h"
#include "util/Log.h"

#include <shellapi.h>

#include <array>
#include <string_view>

namespace threnody::shell {
namespace {

bool isTaskbar(HWND hwnd) noexcept {
    std::array<wchar_t, 64> className{};
    const int length = GetClassNameW(hwnd, className.data(), static_cast<int>(className.size()));
    const std::wstring_view name{className.data(), static_cast<std::size_t>(length)};
    return name == taskbar::taskbarClassName || name == taskbar::secondaryTaskbarClassName;
}

bool isOurs(HWND hwnd) noexcept {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    return processId == GetCurrentProcessId();
}

void launchSpotify() {
    // The protocol handler covers both the Store and the desktop build.
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"spotify:", nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        log::warn("launching Spotify failed (ShellExecute code {})", result);
    } else {
        log::info("launched Spotify");
    }
}

void bringToFront(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    if (SetForegroundWindow(hwnd)) {
        return;
    }
    // Foreground lock: briefly join the current foreground thread's input
    // queue, which grants the right to take foreground.
    const HWND current = GetForegroundWindow();
    const DWORD foregroundThread = current != nullptr ? GetWindowThreadProcessId(current, nullptr) : 0;
    const DWORD ourThread = GetCurrentThreadId();
    if (foregroundThread != 0 && foregroundThread != ourThread && AttachThreadInput(foregroundThread, ourThread, TRUE)) {
        SetForegroundWindow(hwnd);
        AttachThreadInput(foregroundThread, ourThread, FALSE);
    }
}

}  // namespace

void SpotifyWindowToggle::rememberForeground() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || isTaskbar(foreground) || isOurs(foreground)) {
        return;  // Focus went to the bar, not to another app; keep what we had.
    }
    m_foreground = foreground;
}

void SpotifyWindowToggle::assumeSpotifyInFront() noexcept {
    if (const std::optional<SpotifyProcess> spotify = findSpotify(); spotify && spotify->mainWindow != nullptr) {
        m_foreground = spotify->mainWindow;
    }
}

void SpotifyWindowToggle::toggle() {
    const std::optional<SpotifyProcess> spotify = findSpotify();
    if (!spotify) {
        launchSpotify();
        m_foreground = nullptr;
        return;
    }
    const HWND window = spotify->mainWindow;
    if (window == nullptr) {
        // Running without a window (starting up, or closed to the tray): the
        // protocol activation brings its window back.
        launchSpotify();
        m_foreground = nullptr;
        return;
    }

    if (IsIconic(window)) {
        bringToFront(window);
        m_foreground = window;
        log::info("Spotify window restored");
    } else if (m_foreground == window || GetForegroundWindow() == window) {
        ShowWindow(window, SW_MINIMIZE);
        m_foreground = nullptr;
        log::info("Spotify window minimised");
    } else {
        bringToFront(window);
        m_foreground = window;
        log::info("Spotify window brought to front");
    }
}

}  // namespace threnody::shell
