#pragma once

#include <Windows.h>

namespace threnody::shell {

// Toggles Spotify's main window: launches it when closed, restores and raises
// it when minimised or behind, minimises it when it is already in front.
//
// "In front" cannot be read at click time: clicking inside the taskbar hands
// focus to the taskbar first. The caller therefore reports the foreground
// window when the pointer enters the widget (`rememberForeground`), taskbars
// are ignored while remembering, and the remembered value is refreshed after
// every toggle so repeated clicks work without moving the pointer.
class SpotifyWindowToggle {
public:
    void rememberForeground() noexcept;
    void toggle();

private:
    HWND m_foreground{};
};

}  // namespace threnody::shell
