#include "spotify/SpotifyClient.h"

#include "Config.h"
#include "spotify/LoopbackListener.h"
#include "spotify/Pkce.h"
#include "util/Log.h"
#include "util/Text.h"

#include <unknwn.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.h>

#include <Windows.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <mutex>

namespace threnody::spotify {

using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Foundation;
using json = nlohmann::json;

namespace {

constexpr wchar_t authorizeEndpoint[] = L"https://accounts.spotify.com/authorize";
constexpr wchar_t tokenEndpoint[] = L"https://accounts.spotify.com/api/token";
constexpr wchar_t nowPlayingEndpoint[] = L"https://api.spotify.com/v1/me/player/currently-playing";
constexpr wchar_t queueEndpoint[] = L"https://api.spotify.com/v1/me/player/queue";
constexpr std::size_t artworkCacheSize = 4;  // The current cover and the next one, with room to spare.
constexpr unsigned authorizationTimeoutSeconds = 300;
constexpr int minArtworkPx = 128;  // Covers the widget's cover square at high DPI.
constexpr std::uint32_t maxArtworkBytes = std::uint32_t{8} << 20;

std::string describe(const winrt::hresult_error& e) {
    return std::format("0x{:08X} {}", static_cast<unsigned long>(e.code().value), winrt::to_string(e.message()));
}

std::wstring urlEncode(std::wstring_view text) {
    const std::string utf8 = text::toUtf8(text);
    std::wstring out;
    for (const unsigned char c : utf8) {
        const bool unreserved = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out += std::format(L"%{:02X}", c);
        }
    }
    return out;
}

// The smallest album image at least `minArtworkPx` wide, else the widest.
// Spotify lists images widest first; a missing width counts as unknown.
std::wstring pickArtworkUrl(const json& item) {
    const auto album = item.find("album");
    if (album == item.end() || !album->is_object()) {
        return {};
    }
    const auto images = album->find("images");
    if (images == album->end() || !images->is_array()) {
        return {};
    }
    std::string best;
    for (const json& image : *images) {
        if (!image.is_object()) {
            continue;
        }
        const auto url = image.find("url");
        if (url == image.end() || !url->is_string()) {
            continue;
        }
        const auto width = image.find("width");
        const bool bigEnough = width == image.end() || !width->is_number() || width->get<int>() >= minArtworkPx;
        if (best.empty() || bigEnough) {
            best = url->get<std::string>();
        }
    }
    return text::toWide(best);
}

}  // namespace

struct SpotifyClient::Shared {
    mutable std::mutex mutex;
    ChangeHandler onChanged;

    Credentials credentials;
    std::string accessToken;
    std::chrono::steady_clock::time_point accessTokenExpiry{};
    Status status;
    std::optional<TrackLinks> links;
    std::deque<Artwork> artworks;  // Most recent first.
    std::uint32_t queueRequests{};
    std::optional<QueueResult> queue;

    // Pending authorisation.
    std::string pendingVerifier;
    std::string pendingState;
    std::string pendingClientId;
    std::unique_ptr<LoopbackListener> listener;

    void notify() const {
        ChangeHandler handler;
        {
            std::scoped_lock lock{mutex};
            handler = onChanged;
        }
        if (handler) {
            handler();
        }
    }

    void setStatus(AuthState state, std::string detail) {
        {
            std::scoped_lock lock{mutex};
            status = {state, std::move(detail)};
        }
        notify();
    }
};

namespace {

using Shared = SpotifyClient::Shared;

struct TokenResponse {
    std::string accessToken;
    std::string refreshToken;
    int expiresIn{3600};
};

// POSTs a form to the token endpoint and parses the reply. Runs on the
// thread pool; the caller owns error reporting.
IAsyncOperation<winrt::hstring> postForm(winrt::hstring endpoint,
                                         winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> fields) {
    HttpClient client;
    HttpFormUrlEncodedContent content{fields};
    const HttpResponseMessage response = co_await client.PostAsync(Uri{endpoint}, content);
    const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
    if (!response.IsSuccessStatusCode()) {
        throw winrt::hresult_error(E_FAIL, winrt::hstring{L"HTTP "} +
                                               winrt::to_hstring(static_cast<int>(response.StatusCode())) + L": " + body);
    }
    co_return body;
}

TokenResponse parseToken(const winrt::hstring& body) {
    const json j = json::parse(winrt::to_string(body));
    TokenResponse token;
    token.accessToken = j.value("access_token", "");
    token.refreshToken = j.value("refresh_token", "");
    token.expiresIn = j.value("expires_in", 3600);
    if (token.accessToken.empty()) {
        throw winrt::hresult_error(E_FAIL, L"la respuesta no trae access_token");
    }
    return token;
}

void storeToken(const std::shared_ptr<Shared>& shared, const TokenResponse& token, const std::string& clientId) {
    std::scoped_lock lock{shared->mutex};
    shared->credentials.clientId = clientId;
    if (!token.refreshToken.empty()) {
        shared->credentials.refreshToken = token.refreshToken;  // Spotify rotates it.
    }
    shared->accessToken = token.accessToken;
    shared->accessTokenExpiry =
        std::chrono::steady_clock::now() + std::chrono::seconds(std::max(60, token.expiresIn - 60));
}

// Ensures a usable access token, refreshing when needed. Returns empty on
// failure after recording the status.
IAsyncOperation<winrt::hstring> ensureAccessToken(std::shared_ptr<Shared> shared) {
    std::string clientId, refreshToken, accessToken;
    bool fresh = false;
    {
        std::scoped_lock lock{shared->mutex};
        clientId = shared->credentials.clientId;
        refreshToken = shared->credentials.refreshToken;
        accessToken = shared->accessToken;
        fresh = !accessToken.empty() && std::chrono::steady_clock::now() < shared->accessTokenExpiry;
    }
    if (fresh) {
        co_return winrt::to_hstring(accessToken);
    }
    if (clientId.empty() || refreshToken.empty()) {
        co_return winrt::hstring{};
    }

    auto fields = winrt::single_threaded_map<winrt::hstring, winrt::hstring>();
    fields.Insert(L"grant_type", L"refresh_token");
    fields.Insert(L"refresh_token", winrt::to_hstring(refreshToken));
    fields.Insert(L"client_id", winrt::to_hstring(clientId));
    try {
        const TokenResponse token = parseToken(co_await postForm(tokenEndpoint, fields));
        storeToken(shared, token, clientId);
        co_return winrt::to_hstring(token.accessToken);
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify token refresh failed: {}", describe(e));
        shared->setStatus(AuthState::Failed, "token refresh: " + winrt::to_string(e.message()));
        co_return winrt::hstring{};
    } catch (const json::exception& e) {
        log::warn("Spotify token refresh: bad JSON: {}", e.what());
        co_return winrt::hstring{};
    }
}

winrt::fire_and_forget exchangeCode(std::shared_ptr<Shared> shared, std::string code) {
    std::string verifier, clientId;
    {
        std::scoped_lock lock{shared->mutex};
        verifier = shared->pendingVerifier;
        clientId = shared->pendingClientId;
    }
    shared->setStatus(AuthState::Exchanging, "");

    auto fields = winrt::single_threaded_map<winrt::hstring, winrt::hstring>();
    fields.Insert(L"grant_type", L"authorization_code");
    fields.Insert(L"code", winrt::to_hstring(code));
    fields.Insert(L"redirect_uri", config::spotifyRedirectUri);
    fields.Insert(L"client_id", winrt::to_hstring(clientId));
    fields.Insert(L"code_verifier", winrt::to_hstring(verifier));
    try {
        const TokenResponse token = parseToken(co_await postForm(tokenEndpoint, fields));
        storeToken(shared, token, clientId);
        log::info("Spotify connected");
        shared->setStatus(AuthState::Connected, "");
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify code exchange failed: {}", describe(e));
        shared->setStatus(AuthState::Failed, "token exchange: " + winrt::to_string(e.message()));
    } catch (const json::exception& e) {
        shared->setStatus(AuthState::Failed, std::string("unexpected token response: ") + e.what());
    }
}

winrt::fire_and_forget fetchNowPlaying(std::weak_ptr<Shared> weak) {
    auto shared = weak.lock();
    if (!shared) {
        co_return;
    }
    const winrt::hstring token = co_await ensureAccessToken(shared);
    if (token.empty()) {
        co_return;
    }
    try {
        HttpClient client;
        client.DefaultRequestHeaders().Authorization(Headers::HttpCredentialsHeaderValue{L"Bearer", token});
        const HttpResponseMessage response = co_await client.GetAsync(Uri{nowPlayingEndpoint});
        if (response.StatusCode() == HttpStatusCode::NoContent) {
            std::scoped_lock lock{shared->mutex};
            shared->links.reset();
            co_return;
        }
        const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
        if (!response.IsSuccessStatusCode()) {
            log::warn("Spotify currently-playing: HTTP {}", static_cast<int>(response.StatusCode()));
            if (response.StatusCode() == HttpStatusCode::Unauthorized) {
                std::scoped_lock lock{shared->mutex};
                shared->accessToken.clear();  // Force a refresh next time.
            }
            co_return;
        }
        const json j = json::parse(winrt::to_string(body));
        const auto item = j.find("item");
        if (item == j.end() || !item->is_object()) {
            std::scoped_lock lock{shared->mutex};
            shared->links.reset();
            co_return;
        }
        TrackLinks links;
        links.trackName = text::toWide(item->value("name", ""));
        links.trackUri = text::toWide(item->value("uri", ""));
        if (const auto artists = item->find("artists"); artists != item->end() && artists->is_array() && !artists->empty()) {
            links.artistName = text::toWide(artists->front().value("name", ""));
            links.artistUri = text::toWide(artists->front().value("uri", ""));
        }
        links.artworkUrl = pickArtworkUrl(*item);
        {
            std::scoped_lock lock{shared->mutex};
            shared->links = std::move(links);
        }
        shared->notify();
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify currently-playing failed: {}", describe(e));
    } catch (const json::exception& e) {
        log::warn("Spotify currently-playing: bad JSON: {}", e.what());
    }
}

// The next entry of the queue. Tracks name their artists; podcast episodes
// have a show instead.
winrt::fire_and_forget fetchQueue(std::weak_ptr<Shared> weak, std::uint32_t request) {
    auto shared = weak.lock();
    if (!shared) {
        co_return;
    }
    const winrt::hstring token = co_await ensureAccessToken(shared);
    if (token.empty()) {
        co_return;
    }
    QueueResult result{.request = request};
    try {
        HttpClient client;
        client.DefaultRequestHeaders().Authorization(Headers::HttpCredentialsHeaderValue{L"Bearer", token});
        const HttpResponseMessage response = co_await client.GetAsync(Uri{queueEndpoint});
        const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
        if (!response.IsSuccessStatusCode()) {
            log::warn("Spotify queue: HTTP {}", static_cast<int>(response.StatusCode()));
            if (response.StatusCode() == HttpStatusCode::Unauthorized) {
                std::scoped_lock lock{shared->mutex};
                shared->accessToken.clear();
            }
        } else if (const json j = json::parse(winrt::to_string(body)); j.contains("queue") && j["queue"].is_array()) {
            std::string playingUri;
            if (const auto playing = j.find("currently_playing"); playing != j.end() && playing->is_object()) {
                result.playingName = text::toWide(playing->value("name", ""));
                playingUri = playing->value("uri", "");
            }
            // The queue sometimes repeats the playing track at its head.
            const json* found = nullptr;
            for (const json& candidate : j["queue"]) {
                if (candidate.is_object() && (playingUri.empty() || candidate.value("uri", "") != playingUri)) {
                    found = &candidate;
                    break;
                }
            }
            if (found != nullptr) {
                const json& item = *found;
                QueuedTrack next;
                next.name = text::toWide(item.value("name", ""));
                if (const auto artists = item.find("artists");
                    artists != item.end() && artists->is_array() && !artists->empty() && artists->front().is_object()) {
                    next.artist = text::toWide(artists->front().value("name", ""));
                } else if (const auto show = item.find("show"); show != item.end() && show->is_object()) {
                    next.artist = text::toWide(show->value("name", ""));
                }
                next.artworkUrl = pickArtworkUrl(item);
                if (next.artworkUrl.empty()) {
                    // Episodes carry their own images rather than an album's.
                    if (const auto images = item.find("images"); images != item.end()) {
                        next.artworkUrl = pickArtworkUrl(json{{"album", {{"images", *images}}}});
                    }
                }
                result.next = std::move(next);
            }
        }
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify queue failed: {}", describe(e));
    } catch (const json::exception& e) {
        log::warn("Spotify queue: bad JSON: {}", e.what());
    }
    {
        std::scoped_lock lock{shared->mutex};
        shared->queue = std::move(result);
    }
    shared->notify();
}

// Album art lives on Spotify's public image CDN; no token needed.
winrt::fire_and_forget fetchArtwork(std::weak_ptr<Shared> weak, std::wstring url) {
    try {
        HttpClient client;
        const HttpResponseMessage response = co_await client.GetAsync(Uri{url});
        if (!response.IsSuccessStatusCode()) {
            log::warn("Spotify artwork: HTTP {}", static_cast<int>(response.StatusCode()));
            co_return;
        }
        const winrt::Windows::Storage::Streams::IBuffer buffer = co_await response.Content().ReadAsBufferAsync();
        if (buffer.Length() == 0 || buffer.Length() > maxArtworkBytes) {
            log::warn("Spotify artwork: unexpected size {} bytes", buffer.Length());
            co_return;
        }
        auto shared = weak.lock();
        if (!shared) {
            co_return;
        }
        {
            std::scoped_lock lock{shared->mutex};
            shared->artworks.push_front(
                Artwork{.url = std::move(url), .bytes = {buffer.data(), buffer.data() + buffer.Length()}});
            if (shared->artworks.size() > artworkCacheSize) {
                shared->artworks.pop_back();
            }
        }
        shared->notify();
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify artwork failed: {}", describe(e));
    }
}

}  // namespace

SpotifyClient::SpotifyClient(ChangeHandler onChanged) : m_shared(std::make_shared<Shared>()) {
    m_shared->onChanged = std::move(onChanged);
}

SpotifyClient::~SpotifyClient() {
    std::unique_ptr<LoopbackListener> listener;
    {
        std::scoped_lock lock{m_shared->mutex};
        m_shared->onChanged = nullptr;
        listener = std::move(m_shared->listener);
    }
    listener.reset();  // Joins its thread outside the lock.
}

void SpotifyClient::setCredentials(Credentials credentials) {
    const bool usable = !credentials.clientId.empty() && !credentials.refreshToken.empty();
    {
        std::scoped_lock lock{m_shared->mutex};
        m_shared->credentials = std::move(credentials);
        m_shared->accessToken.clear();
        m_shared->status = {usable ? AuthState::Connected : AuthState::Disconnected, ""};
    }
}

Credentials SpotifyClient::credentials() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->credentials;
}

Status SpotifyClient::status() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->status;
}

bool SpotifyClient::connected() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->status.state == AuthState::Connected;
}

void SpotifyClient::beginAuthorization(std::string clientId) {
    Result<std::string> verifier = pkce::randomToken();
    Result<std::string> state = pkce::randomToken(16);
    if (!verifier || !state) {
        m_shared->setStatus(AuthState::Failed, (verifier ? state : verifier).error().describe());
        return;
    }
    Result<std::string> challenge = pkce::challenge(*verifier);
    if (!challenge) {
        m_shared->setStatus(AuthState::Failed, challenge.error().describe());
        return;
    }

    std::weak_ptr<Shared> weak = m_shared;
    auto listener = std::make_unique<LoopbackListener>(
        config::spotifyRedirectPort, "/callback", authorizationTimeoutSeconds, [weak](LoopbackListener::Redirect redirect) {
            auto shared = weak.lock();
            if (!shared) {
                return;
            }
            if (!redirect.error.empty()) {
                shared->setStatus(AuthState::Failed, redirect.error);
                return;
            }
            std::string expectedState;
            {
                std::scoped_lock lock{shared->mutex};
                expectedState = shared->pendingState;
            }
            if (const auto error = redirect.query.find("error"); error != redirect.query.end()) {
                shared->setStatus(AuthState::Failed, "Spotify: " + error->second);
                return;
            }
            const auto code = redirect.query.find("code");
            const auto state = redirect.query.find("state");
            if (code == redirect.query.end() || state == redirect.query.end() || state->second != expectedState) {
                shared->setStatus(AuthState::Failed, "invalid authorisation response (state mismatch)");
                return;
            }
            exchangeCode(shared, code->second);
        });

    {
        std::scoped_lock lock{m_shared->mutex};
        m_shared->pendingVerifier = *verifier;
        m_shared->pendingState = *state;
        m_shared->pendingClientId = clientId;
        m_shared->listener = std::move(listener);
    }

    const std::wstring url = std::format(
        L"{}?client_id={}&response_type=code&redirect_uri={}&code_challenge_method=S256&code_challenge={}&state={}&scope={}",
        authorizeEndpoint, urlEncode(text::toWide(clientId)), urlEncode(config::spotifyRedirectUri),
        text::toWide(*challenge), text::toWide(*state), urlEncode(config::spotifyScopes));
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    log::info("Spotify authorisation started in the browser");
    m_shared->setStatus(AuthState::WaitingForBrowser, "");
}

void SpotifyClient::disconnect() {
    std::unique_ptr<LoopbackListener> listener;
    {
        std::scoped_lock lock{m_shared->mutex};
        m_shared->credentials = {};
        m_shared->accessToken.clear();
        m_shared->links.reset();
        m_shared->artworks.clear();
        m_shared->queue.reset();
        m_shared->status = {AuthState::Disconnected, ""};
        listener = std::move(m_shared->listener);
    }
    listener.reset();
    log::info("Spotify disconnected");
    m_shared->notify();
}

void SpotifyClient::requestNowPlaying() {
    if (connected()) {
        fetchNowPlaying(m_shared);
    }
}

std::optional<TrackLinks> SpotifyClient::links() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->links;
}

void SpotifyClient::requestArtwork(std::wstring url) {
    if (url.empty() || artwork(url)) {
        return;
    }
    fetchArtwork(m_shared, std::move(url));
}

std::optional<Artwork> SpotifyClient::artwork(const std::wstring& url) const {
    std::scoped_lock lock{m_shared->mutex};
    for (const Artwork& artwork : m_shared->artworks) {
        if (artwork.url == url) {
            return artwork;
        }
    }
    return std::nullopt;
}

std::uint32_t SpotifyClient::requestQueue() {
    std::uint32_t request{};
    {
        std::scoped_lock lock{m_shared->mutex};
        request = ++m_shared->queueRequests;
    }
    if (connected()) {
        fetchQueue(m_shared, request);
    }
    return request;
}

std::optional<QueueResult> SpotifyClient::queue() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->queue;
}

}  // namespace threnody::spotify
