#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

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
};

// Spotify Web API client: authorisation code flow with PKCE (no client
// secret), token refresh, and the one call the widget needs, "currently
// playing". Network work runs on the WinRT thread pool; every state change
// calls the handler, and the UI thread reads snapshots.
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

    struct Shared;

private:
    std::shared_ptr<Shared> m_shared;
};

}  // namespace threnody::spotify
