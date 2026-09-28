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

#include <algorithm>
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
// The player state rather than "currently playing": the same item, plus the
// (undocumented) smart_shuffle flag that nothing else exposes.
constexpr wchar_t nowPlayingEndpoint[] = L"https://api.spotify.com/v1/me/player";
constexpr wchar_t queueEndpoint[] = L"https://api.spotify.com/v1/me/player/queue";
constexpr std::size_t artworkCacheSize = 6;  // Current and next covers, an artist photo, room to spare.
constexpr std::size_t artistCacheSize = 4;
constexpr wchar_t artistsEndpoint[] = L"https://api.spotify.com/v1/artists/";
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

// The smallest image at least `minArtworkPx` wide, else the widest.
// Spotify lists images widest first; a missing width counts as unknown.
std::wstring pickImageUrl(const json& images) {
    if (!images.is_array()) {
        return {};
    }
    std::string best;
    for (const json& image : images) {
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

std::wstring pickArtworkUrl(const json& item) {
    const auto album = item.find("album");
    if (album == item.end() || !album->is_object()) {
        return {};
    }
    const auto images = album->find("images");
    return images == album->end() ? std::wstring{} : pickImageUrl(*images);
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
    std::deque<ArtistInfo> artists;         // Most recent first.
    std::vector<std::wstring> artistsAsked;  // In flight, so a second ask waits for the first.
    std::optional<PlayerModes> modes;
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
            shared->modes.reset();
            co_return;
        }
        const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
        if (!response.IsSuccessStatusCode()) {
            log::warn("Spotify player state: HTTP {}", static_cast<int>(response.StatusCode()));
            if (response.StatusCode() == HttpStatusCode::Unauthorized) {
                std::scoped_lock lock{shared->mutex};
                shared->accessToken.clear();  // Force a refresh next time.
            }
            co_return;
        }
        const json j = json::parse(winrt::to_string(body));
        {
            PlayerModes modes;
            if (const auto shuffle = j.find("shuffle_state"); shuffle != j.end() && shuffle->is_boolean()) {
                modes.shuffle = shuffle->get<bool>();
            }
            if (const auto smart = j.find("smart_shuffle"); smart != j.end() && smart->is_boolean()) {
                modes.smartShuffle = smart->get<bool>();
            }
            if (const auto repeat = j.find("repeat_state"); repeat != j.end() && repeat->is_string()) {
                const std::string value = repeat->get<std::string>();
                modes.repeat = value == "track" ? RepeatMode::One : value == "context" ? RepeatMode::All : RepeatMode::Off;
            }
            std::scoped_lock lock{shared->mutex};
            if (shared->modes != modes) {
                log::info("Spotify modes: shuffle {}{}, repeat {}", modes.shuffle ? "on" : "off",
                          modes.smartShuffle ? " (smart)" : "",
                          modes.repeat == RepeatMode::One   ? "one"
                          : modes.repeat == RepeatMode::All ? "all"
                                                            : "off");
            }
            shared->modes = modes;
        }
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
            links.artistId = text::toWide(artists->front().value("id", ""));
            for (const json& artist : *artists) {
                if (artist.is_object()) {
                    links.artistNames.push_back(text::toWide(artist.value("name", "")));
                }
            }
        }
        links.artworkUrl = pickArtworkUrl(*item);
        if (const auto album = item->find("album"); album != item->end() && album->is_object()) {
            links.albumName = text::toWide(album->value("name", ""));
            links.releaseDate = text::toWide(album->value("release_date", ""));
            links.albumTracks = album->value("total_tracks", 0);
        }
        links.durationMs = item->value("duration_ms", std::int64_t{0});
        links.explicitContent = item->value("explicit", false);
        links.trackNumber = item->value("track_number", 0);
        {
            std::scoped_lock lock{shared->mutex};
            shared->links = std::move(links);
        }
        shared->notify();
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify player state failed: {}", describe(e));
    } catch (const json::exception& e) {
        log::warn("Spotify player state: bad JSON: {}", e.what());
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

// An artist and their newest release: the most recent date among the first
// ten albums and singles the albums endpoint returns.
winrt::fire_and_forget fetchArtist(std::weak_ptr<Shared> weak, std::wstring id) {
    auto shared = weak.lock();
    if (!shared) {
        co_return;
    }
    // However this ends, the artist may be asked for again.
    struct Done {
        std::shared_ptr<Shared> shared;
        std::wstring id;
        ~Done() {
            std::scoped_lock lock{shared->mutex};
            std::erase(shared->artistsAsked, id);
        }
    } done{shared, id};
    const winrt::hstring token = co_await ensureAccessToken(shared);
    if (token.empty()) {
        co_return;
    }
    try {
        HttpClient client;
        client.DefaultRequestHeaders().Authorization(Headers::HttpCredentialsHeaderValue{L"Bearer", token});
        const std::wstring base = std::wstring{artistsEndpoint} + id;
        HttpResponseMessage response = co_await client.GetAsync(Uri{base});
        winrt::hstring body = co_await response.Content().ReadAsStringAsync();
        if (!response.IsSuccessStatusCode()) {
            log::warn("Spotify artist: HTTP {}", static_cast<int>(response.StatusCode()));
            co_return;
        }
        const json artist = json::parse(winrt::to_string(body));
        ArtistInfo info;
        info.id = id;
        info.name = text::toWide(artist.value("name", ""));
        if (const auto images = artist.find("images"); images != artist.end()) {
            info.imageUrl = pickImageUrl(*images);
        }
        if (const auto genres = artist.find("genres"); genres != artist.end() && genres->is_array()) {
            for (const json& genre : *genres) {
                if (genre.is_string()) {
                    info.genres.push_back(text::toWide(genre.get<std::string>()));
                }
            }
        }

        response = co_await client.GetAsync(Uri{base + L"/albums?include_groups=album,single&limit=10"});
        body = co_await response.Content().ReadAsStringAsync();
        if (response.IsSuccessStatusCode()) {
            const json albums = json::parse(winrt::to_string(body));
            std::string newestDate;
            if (const auto items = albums.find("items"); items != albums.end() && items->is_array()) {
                for (const json& album : *items) {
                    if (!album.is_object()) {
                        continue;
                    }
                    const std::string date = album.value("release_date", "");
                    if (date > newestDate) {  // ISO dates compare as text.
                        newestDate = date;
                        info.latestRelease = text::toWide(album.value("name", ""));
                        info.latestReleaseDate = text::toWide(date);
                    }
                }
            }
        }
        log::info("Spotify artist: {} ({} genres, latest release {})", text::toUtf8(info.name), info.genres.size(),
                  info.latestReleaseDate.empty() ? "unknown" : text::toUtf8(info.latestReleaseDate));
        {
            std::scoped_lock lock{shared->mutex};
            std::erase_if(shared->artists, [&](const ArtistInfo& a) { return a.id == info.id; });
            shared->artists.push_front(std::move(info));
            if (shared->artists.size() > artistCacheSize) {
                shared->artists.pop_back();
            }
        }
        shared->notify();
    } catch (const winrt::hresult_error& e) {
        log::warn("Spotify artist failed: {}", describe(e));
    } catch (const json::exception& e) {
        log::warn("Spotify artist: bad JSON: {}", e.what());
    }
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
        m_shared->artists.clear();
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

std::optional<PlayerModes> SpotifyClient::modes() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->modes;
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

void SpotifyClient::requestArtist(std::wstring id) {
    if (id.empty() || artist(id) || !connected()) {
        return;
    }
    {
        std::scoped_lock lock{m_shared->mutex};
        if (std::ranges::find(m_shared->artistsAsked, id) != m_shared->artistsAsked.end()) {
            return;
        }
        m_shared->artistsAsked.push_back(id);
    }
    fetchArtist(m_shared, std::move(id));
}

std::optional<ArtistInfo> SpotifyClient::artist(const std::wstring& id) const {
    std::scoped_lock lock{m_shared->mutex};
    for (const ArtistInfo& info : m_shared->artists) {
        if (info.id == id) {
            return info;
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
