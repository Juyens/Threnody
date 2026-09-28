#pragma once

#include "media/RepeatMode.h"

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

    // For the song's info bubble.
    std::wstring artistId;                  // First artist, for requestArtist().
    std::vector<std::wstring> artistNames;  // All of them.
    std::wstring albumName;
    std::wstring releaseDate;  // "2021-05-14", "2021-05" or "2021".
    std::int64_t durationMs{};
    bool explicitContent{false};
    int trackNumber{};
    int albumTracks{};
};

// An artist, for their info bubble. Spotify no longer gives followers or
// popularity (February 2026); genres are often empty.
struct ArtistInfo {
    std::wstring id;
    std::wstring name;
    std::wstring imageUrl;
    std::vector<std::wstring> genres;
    std::wstring latestRelease;      // The newest album or single...
    std::wstring latestReleaseDate;  // ...and its date.
};

// The first track in the user's queue: what "next" will play.
struct QueuedTrack {
    std::wstring name;
    std::wstring artist;  // For a podcast episode, the show.
    std::wstring artworkUrl;
};

// Answer to one requestQueue(): `next` is empty when the queue is empty or
// could not be read. `playingName` is what Spotify thinks is playing; the
// Web API lags SMTC by a moment after a skip, and a queue read in that
// moment still lists the new track as next, so callers compare the two.
struct QueueResult {
    std::uint32_t request{};
    std::wstring playingName;
    std::optional<QueuedTrack> next;
};

// Shuffle and repeat as the Web API reports them: the reliable view, where
// SMTC is seconds late and cannot tell smart shuffle from shuffle.
struct PlayerModes {
    bool shuffle{false};
    bool smartShuffle{false};
    RepeatMode repeat{RepeatMode::Off};

    bool operator==(const PlayerModes&) const = default;
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

    // Fetches the player state: what is playing (result in `links()`) and
    // the shuffle and repeat modes (`modes()`, empty until known).
    void requestNowPlaying();
    [[nodiscard]] std::optional<TrackLinks> links() const;
    [[nodiscard]] std::optional<PlayerModes> modes() const;

    // Fetches the queue; the answer appears in `queue()` tagged with the
    // returned request number, so a stale answer can be told apart.
    std::uint32_t requestQueue();
    [[nodiscard]] std::optional<QueueResult> queue() const;

    // Fetches an artist and their newest release; the answer appears in
    // `artist(id)`. Kept for the last few artists asked for.
    void requestArtist(std::wstring id);
    [[nodiscard]] std::optional<ArtistInfo> artist(const std::wstring& id) const;

    // Downloads `url` (an artworkUrl) unless it is among the few kept; the
    // result appears in `artwork(url)`.
    void requestArtwork(std::wstring url);
    [[nodiscard]] std::optional<Artwork> artwork(const std::wstring& url) const;

    struct Shared;

private:
    std::shared_ptr<Shared> m_shared;
};

}  // namespace threnody::spotify
