#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace threnody::media {

// Snapshot of what Spotify reports through the System Media Transport
// Controls. `available` is false when Spotify has no session (not running, or
// nothing has been played yet).
struct NowPlaying {
    bool available{false};
    std::wstring title;
    std::wstring artist;
    bool playing{false};
    std::vector<std::uint8_t> cover;  // Encoded image bytes, empty if none.
    std::uint32_t coverVersion{};     // Bumps whenever `cover` changes.
};

enum class TransportCommand { Previous, TogglePlayPause, Next };

// Wraps Windows.Media.Control for the Spotify session only. Change
// notifications arrive on WinRT thread-pool threads: the handler must do no
// more than wake the UI thread, which then calls `snapshot()`.
class MediaSession {
public:
    using ChangeHandler = std::function<void()>;

    explicit MediaSession(ChangeHandler onChanged);
    ~MediaSession();

    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    [[nodiscard]] NowPlaying snapshot() const;
    void send(TransportCommand command) const;

    struct Shared;

private:
    std::shared_ptr<Shared> m_shared;
};

}  // namespace threnody::media
