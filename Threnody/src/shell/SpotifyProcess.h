#pragma once

#include <Windows.h>

#include <optional>

namespace threnody::shell {

struct SpotifyProcess {
    DWORD processId{};
    HWND mainWindow{};  // Null when Spotify has no visible top-level window.
};

// Spotify runs several processes. The one owning the main window is the root
// of the tree, which is what process loopback needs; with no window (still
// starting, or hidden) the oldest process stands in.
[[nodiscard]] std::optional<SpotifyProcess> findSpotify();

}  // namespace threnody::shell
