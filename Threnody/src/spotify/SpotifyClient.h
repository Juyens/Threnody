#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace threnody::spotify {

struct Credentials {
    std::string clientId;
    std::string refreshToken;  // Plain text in memory; the caller protects it at rest.

    bool operator==(const Credentials&) const = default;
};

enum class AuthState { Disconnected, WaitingForBrowser, Exchanging, Connected, Failed };

struct Status {
    AuthState state{AuthState::Disconnected};
    std::string detail;  // UTF-8, shown in the settings window.
};

// Exact Spotify ids for what is playing, from the Web API.
struct TrackLinks {
    std::wstring trackName;
    std::wstring artistName;
    std::wstring trackUri;   // spotify:track:...
    std::wstring artistUri;  // spotify:artist:...
    std::wstring artworkUrl;  // Album art, the widget's fallback when SMTC has none.
};

// The first track in the user's queue: what "next" will play.
struct QueuedTrack {
    std::wstring name;
    std::wstring artist;  // For a podcast episode, the show.
    std::wstring artworkUrl;
};

// Answer to one requestQueue(): `next` is empty when the queue is empty or
// could not be read.
struct QueueResult {
    std::uint32_t request{};
    std::optional<QueuedTrack> next;
};

// Downloaded album art, tagged with the URL it came from.
struct Artwork {
    std::wstring url;
    std::vector<std::uint8_t> bytes;  // Encoded image.
};

// Spotify Web API client: authorisation code flow with PKCE (no client
// secret), token refresh, and the calls the widget needs: "currently
// playing", the queue, and album art. Network work runs on the WinRT thread
// pool; every state change calls the handler, and the UI thread reads
// snapshots.
class SpotifyClient {
public:
    using ChangeHandler = std::function<void()>;

    explicit SpotifyClient(ChangeHandler onChanged);
    ~SpotifyClient();

    SpotifyClient(const SpotifyClient&) = delete;
    SpotifyClient& operator=(const SpotifyClient&) = delete;

    // Restores a saved connection.
    void setCredentials(Credentials credentials);
    [[nodiscard]] Credentials credentials() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] bool connected() const;

    // Opens the browser on Spotify's consent page and waits for the redirect.
    void beginAuthorization(std::string clientId);
    void disconnect();

    // Fetches what is playing; result appears in `links()`.
    void requestNowPlaying();
    [[nodiscard]] std::optional<TrackLinks> links() const;

    // Fetches the queue; the answer appears in `queue()` tagged with the
    // returned request number, so a stale answer can be told apart.
    std::uint32_t requestQueue();
    [[nodiscard]] std::optional<QueueResult> queue() const;

    // Downloads `url` (an artworkUrl) unless it is among the few kept; the
    // result appears in `artwork(url)`.
    void requestArtwork(std::wstring url);
    [[nodiscard]] std::optional<Artwork> artwork(const std::wstring& url) const;

    struct Shared;

private:
    std::shared_ptr<Shared> m_shared;
};

}  // namespace threnody::spotify
