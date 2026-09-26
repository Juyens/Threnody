#pragma once

#include <Windows.h>
#include <unknwn.h>
#include <audiopolicy.h>
#include <winrt/base.h>

#include <optional>
#include <vector>

namespace threnody::audio {

struct VolumeState {
    float level{};  // [0, 1], the session volume shown in the Windows mixer.
    bool muted{false};
};

// Spotify's own volume in the Windows volume mixer: the audio sessions owned
// by any Spotify.exe, on every active output device. Leaves the system volume
// and Spotify's in-app slider alone, and needs no Web API or Premium.
//
// The session list is cached so a slider drag does not enumerate devices on
// every move; `refresh` rebuilds it (Spotify restarted, device switched).
// Use from the UI thread, which is a COM apartment.
class SpotifyVolume {
public:
    void refresh();

    // Empty when Spotify has no audio session (not running, or never played).
    [[nodiscard]] std::optional<VolumeState> read();

    // Apply to every Spotify session. False when there was none.
    bool setLevel(float level);
    bool setMuted(bool muted);

private:
    // Runs `apply` on every cached session, refreshing once if the cache is
    // empty or every call failed (sessions die with Spotify).
    template <typename Apply>
    bool forEachSession(Apply apply);

    std::vector<winrt::com_ptr<ISimpleAudioVolume>> m_sessions;  // Playing session first.
};

}  // namespace threnody::audio
