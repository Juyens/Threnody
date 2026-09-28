#include "media/MediaSession.h"

#include "media/SourceAppId.h"
#include "util/Log.h"

#include <unknwn.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <mutex>

namespace threnody::media {

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

namespace {

constexpr std::uint64_t maxCoverBytes = std::uint64_t{8} << 20;

std::string describe(const winrt::hresult_error& e) {
    return std::format("0x{:08X} {}", static_cast<unsigned long>(e.code().value), winrt::to_string(e.message()));
}

}  // namespace

// Everything the coroutines touch. Owned jointly by the MediaSession and any
// in-flight coroutine, so a callback landing after the session is gone finds
// a cleared handler instead of a dangling pointer.
struct MediaSession::Shared {
    mutable std::mutex mutex;
    NowPlaying state;
    ChangeHandler onChanged;

    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    GlobalSystemMediaTransportControlsSession session{nullptr};
    winrt::event_token sessionsChangedToken{};
    winrt::event_token propertiesChangedToken{};
    winrt::event_token playbackChangedToken{};
    winrt::event_token timelineChangedToken{};

    // Each refresh gets a generation. Spotify raises several property events
    // per track (text first, artwork later), so every refresh publishes as
    // soon as it has something, and a result is only dropped when a newer
    // refresh has already published that part.
    std::uint32_t propertiesGeneration{};
    std::uint32_t publishedTextGeneration{};
    std::uint32_t publishedCoverGeneration{};

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
};

namespace {

using Shared = MediaSession::Shared;

void refreshPlayback(const std::shared_ptr<Shared>& shared) {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    {
        std::scoped_lock lock{shared->mutex};
        session = shared->session;
    }
    if (!session) {
        return;
    }
    try {
        const auto info = session.GetPlaybackInfo();
        const bool playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        std::optional<bool> shuffle;
        if (info.Controls().IsShuffleEnabled()) {
            if (const auto active = info.IsShuffleActive()) {
                shuffle = active.Value();
            }
        }
        std::optional<RepeatMode> repeat;
        if (info.Controls().IsRepeatEnabled()) {
            if (const auto mode = info.AutoRepeatMode()) {
                switch (mode.Value()) {
                    case winrt::Windows::Media::MediaPlaybackAutoRepeatMode::None: repeat = RepeatMode::Off; break;
                    case winrt::Windows::Media::MediaPlaybackAutoRepeatMode::List: repeat = RepeatMode::All; break;
                    case winrt::Windows::Media::MediaPlaybackAutoRepeatMode::Track: repeat = RepeatMode::One; break;
                }
            }
        }
        // The timeline: TimeSpans are 100 ns ticks; DateTime counts 100 ns
        // ticks from 1601, 11644473600 seconds before the Unix epoch.
        const auto timeline = session.GetTimelineProperties();
        const std::int64_t positionMs = timeline.Position().count() / 10000;
        const std::int64_t durationMs = (timeline.EndTime() - timeline.StartTime()).count() / 10000;
        const std::int64_t positionAtMs =
            timeline.LastUpdatedTime().time_since_epoch().count() / 10000 - std::int64_t{11644473600} * 1000;
        bool changed = false;
        {
            std::scoped_lock lock{shared->mutex};
            NowPlaying& state = shared->state;
            if (state.positionMs != positionMs || state.durationMs != durationMs || state.positionAtMs != positionAtMs) {
                state.positionMs = positionMs;
                state.durationMs = durationMs;
                state.positionAtMs = positionAtMs;
                changed = true;
            }
            if (shared->state.shuffle != shuffle || shared->state.repeat != repeat) {
                log::info("SMTC modes: shuffle {}, repeat {}", shuffle ? (*shuffle ? "on" : "off") : "n/a",
                          !repeat                     ? "n/a"
                          : *repeat == RepeatMode::One ? "one"
                          : *repeat == RepeatMode::All ? "all"
                                                       : "off");
            }
            changed = changed || shared->state.playing != playing || shared->state.shuffle != shuffle ||
                      shared->state.repeat != repeat;
            shared->state.playing = playing;
            shared->state.shuffle = shuffle;
            shared->state.repeat = repeat;
        }
        if (changed) {
            shared->notify();
        }
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC GetPlaybackInfo failed: {}", describe(e));
    }
}

winrt::fire_and_forget refreshProperties(std::weak_ptr<Shared> weak) {
    auto shared = weak.lock();
    if (!shared) {
        co_return;
    }

    GlobalSystemMediaTransportControlsSession session{nullptr};
    std::uint32_t generation{};
    {
        std::scoped_lock lock{shared->mutex};
        session = shared->session;
        generation = ++shared->propertiesGeneration;
    }
    if (!session) {
        co_return;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto elapsedMs = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    };

    try {
        const auto properties = co_await session.TryGetMediaPropertiesAsync();
        std::wstring title{properties.Title()};
        std::wstring artist{properties.Artist()};

        // Text first: the widget should not wait for the artwork download.
        bool textChanged = false;
        bool haveCover = false;
        {
            std::scoped_lock lock{shared->mutex};
            if (generation > shared->publishedTextGeneration) {
                shared->publishedTextGeneration = generation;
                NowPlaying& state = shared->state;
                textChanged = !state.available || state.title != title || state.artist != artist;
                state.available = true;
                state.title = std::move(title);
                state.artist = std::move(artist);
                if (textChanged) {
                    state.coverPending = true;  // The old artwork must not stand in for the new track.
                }
            }
            haveCover = !shared->state.cover.empty() && !shared->state.coverPending;
        }
        if (textChanged) {
            log::info("SMTC properties #{}: text after {} ms", generation, elapsedMs());
            shared->notify();
        }
        // This also runs from a 2 s poll; downloading the same artwork each
        // time would be wasteful. Spotify changes artwork with the text.
        if (!textChanged && haveCover) {
            co_return;
        }

        std::vector<std::uint8_t> cover;
        if (const auto thumbnail = properties.Thumbnail()) {
            const auto stream = co_await thumbnail.OpenReadAsync();
            const std::uint64_t size = stream.Size();
            if (size > 0 && size < maxCoverBytes) {
                // A single read may come back short; a truncated image would
                // fail to decode and leave the placeholder up.
                cover.reserve(static_cast<std::size_t>(size));
                Buffer buffer{static_cast<std::uint32_t>(size)};
                while (cover.size() < size) {
                    const auto want = static_cast<std::uint32_t>(size - cover.size());
                    const auto read = co_await stream.ReadAsync(buffer, want, InputStreamOptions::None);
                    if (read.Length() == 0) {
                        break;
                    }
                    cover.insert(cover.end(), read.data(), read.data() + read.Length());
                }
                if (cover.size() < size) {
                    log::warn("SMTC artwork short read: {} of {} bytes", cover.size(), size);
                    cover.clear();
                }
            }
        }

        bool coverChanged = false;
        const bool publishedEmpty = cover.empty();
        {
            std::scoped_lock lock{shared->mutex};
            // Only artwork read for the current text may settle it.
            if (generation > shared->publishedCoverGeneration && generation >= shared->publishedTextGeneration) {
                shared->publishedCoverGeneration = generation;
                NowPlaying& state = shared->state;
                if (state.cover != cover || state.coverPending) {
                    state.cover = std::move(cover);
                    ++state.coverVersion;  // Bumped even for identical bytes, so a pending placeholder is replaced.
                    coverChanged = true;
                }
                state.coverPending = false;
            }
        }
        if (coverChanged) {
            log::info("SMTC properties #{}: {} after {} ms", generation, publishedEmpty ? "no cover" : "cover",
                      elapsedMs());
            shared->notify();
        }
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC TryGetMediaPropertiesAsync failed: {}", describe(e));
    }
}

// Chooses Spotify's session among the manager's, rewiring the per-session
// events when it changes.
void pickSession(const std::shared_ptr<Shared>& shared) {
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    {
        std::scoped_lock lock{shared->mutex};
        manager = shared->manager;
    }
    if (!manager) {
        return;
    }

    GlobalSystemMediaTransportControlsSession chosen{nullptr};
    try {
        for (const auto& candidate : manager.GetSessions()) {
            if (isSpotifySource(candidate.SourceAppUserModelId())) {
                chosen = candidate;
                break;
            }
        }
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC GetSessions failed: {}", describe(e));
        return;
    }

    bool changed = false;
    bool sourceChanged = false;
    {
        std::scoped_lock lock{shared->mutex};
        // Spotify recreates its SMTC session now and then (SessionsChanged
        // fires, GetSessions hands out a new object for the same id) and the
        // old object stops raising events. So the handlers must move to the
        // new object whenever the *object* differs; the id only decides
        // whether this is worth a log line.
        if (shared->session == chosen) {
            return;
        }
        sourceChanged = static_cast<bool>(shared->session) != static_cast<bool>(chosen) ||
                        (chosen && shared->session.SourceAppUserModelId() != chosen.SourceAppUserModelId());
        if (shared->session) {
            shared->session.MediaPropertiesChanged(shared->propertiesChangedToken);
            shared->session.PlaybackInfoChanged(shared->playbackChangedToken);
            shared->session.TimelinePropertiesChanged(shared->timelineChangedToken);
        }
        shared->session = chosen;
        changed = true;

        if (chosen) {
            std::weak_ptr<Shared> weak = shared;
            shared->propertiesChangedToken = chosen.MediaPropertiesChanged([weak](const auto&, const auto&) {
                refreshProperties(weak);
            });
            shared->playbackChangedToken = chosen.PlaybackInfoChanged([weak](const auto&, const auto&) {
                if (auto s = weak.lock()) {
                    refreshPlayback(s);
                }
            });
            shared->timelineChangedToken = chosen.TimelinePropertiesChanged([weak](const auto&, const auto&) {
                if (auto s = weak.lock()) {
                    refreshPlayback(s);
                }
            });
        } else {
            shared->state = NowPlaying{.coverVersion = shared->state.coverVersion + 1};
        }
    }

    if (changed) {
        if (sourceChanged) {
            log::info("SMTC Spotify session {}", chosen ? "found" : "gone");
        } else {
            log::info("SMTC session object replaced; handlers moved");
        }
        if (chosen) {
            refreshPlayback(shared);
            refreshProperties(shared);
        } else {
            shared->notify();
        }
    }
}

winrt::fire_and_forget start(std::weak_ptr<Shared> weak) {
    try {
        const auto manager = co_await GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        auto shared = weak.lock();
        if (!shared) {
            co_return;
        }
        {
            std::scoped_lock lock{shared->mutex};
            shared->manager = manager;
            shared->sessionsChangedToken = manager.SessionsChanged([weak](const auto&, const auto&) {
                if (auto s = weak.lock()) {
                    // Spotify raises this on track changes too; the session
                    // may be the same object, so refresh regardless.
                    pickSession(s);
                    refreshPlayback(s);
                    refreshProperties(s);
                }
            });
        }
        pickSession(shared);
    } catch (const winrt::hresult_error& e) {
        log::error("SMTC manager unavailable: {}", describe(e));
    }
}

constexpr const char* commandName(TransportCommand command) noexcept {
    switch (command) {
        case TransportCommand::Previous: return "previous";
        case TransportCommand::TogglePlayPause: return "toggle play/pause";
        case TransportCommand::Next: return "next";
    }
    return "?";
}

winrt::fire_and_forget sendCommand(GlobalSystemMediaTransportControlsSession session, TransportCommand command) {
    try {
        bool accepted = false;
        switch (command) {
            case TransportCommand::Previous: accepted = co_await session.TrySkipPreviousAsync(); break;
            case TransportCommand::TogglePlayPause: accepted = co_await session.TryTogglePlayPauseAsync(); break;
            case TransportCommand::Next: accepted = co_await session.TrySkipNextAsync(); break;
        }
        log::info("SMTC {} {}", commandName(command), accepted ? "accepted" : "rejected");
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC {} failed: {}", commandName(command), describe(e));
    }
}

winrt::fire_and_forget sendShuffle(GlobalSystemMediaTransportControlsSession session, bool active) {
    try {
        const bool accepted = co_await session.TryChangeShuffleActiveAsync(active);
        log::info("SMTC shuffle {} {}", active ? "on" : "off", accepted ? "accepted" : "rejected");
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC shuffle failed: {}", describe(e));
    }
}

winrt::fire_and_forget sendRepeat(GlobalSystemMediaTransportControlsSession session, RepeatMode mode) {
    using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
    const MediaPlaybackAutoRepeatMode target = mode == RepeatMode::One   ? MediaPlaybackAutoRepeatMode::Track
                                               : mode == RepeatMode::All ? MediaPlaybackAutoRepeatMode::List
                                                                         : MediaPlaybackAutoRepeatMode::None;
    try {
        const bool accepted = co_await session.TryChangeAutoRepeatModeAsync(target);
        log::info("SMTC repeat {} {}", mode == RepeatMode::One ? "one" : mode == RepeatMode::All ? "all" : "off",
                  accepted ? "accepted" : "rejected");
    } catch (const winrt::hresult_error& e) {
        log::warn("SMTC repeat failed: {}", describe(e));
    }
}

}  // namespace

MediaSession::MediaSession(ChangeHandler onChanged) : m_shared(std::make_shared<Shared>()) {
    m_shared->onChanged = std::move(onChanged);
    start(m_shared);
}

MediaSession::~MediaSession() {
    std::scoped_lock lock{m_shared->mutex};
    m_shared->onChanged = nullptr;
    if (m_shared->session) {
        m_shared->session.MediaPropertiesChanged(m_shared->propertiesChangedToken);
        m_shared->session.PlaybackInfoChanged(m_shared->playbackChangedToken);
        m_shared->session.TimelinePropertiesChanged(m_shared->timelineChangedToken);
        m_shared->session = nullptr;
    }
    if (m_shared->manager) {
        m_shared->manager.SessionsChanged(m_shared->sessionsChangedToken);
        m_shared->manager = nullptr;
    }
}

NowPlaying MediaSession::snapshot() const {
    std::scoped_lock lock{m_shared->mutex};
    return m_shared->state;
}

void MediaSession::poll() const {
    pickSession(m_shared);
    bool haveSession = false;
    {
        std::scoped_lock lock{m_shared->mutex};
        haveSession = static_cast<bool>(m_shared->session);
    }
    if (haveSession) {
        refreshPlayback(m_shared);
        refreshProperties(m_shared);
    }
}

void MediaSession::send(TransportCommand command) const {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    {
        std::scoped_lock lock{m_shared->mutex};
        session = m_shared->session;
    }
    if (session) {
        sendCommand(session, command);
    }
}

void MediaSession::setShuffle(bool active) const {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    {
        std::scoped_lock lock{m_shared->mutex};
        session = m_shared->session;
    }
    if (session) {
        sendShuffle(session, active);
    }
}

void MediaSession::setRepeat(RepeatMode mode) const {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    {
        std::scoped_lock lock{m_shared->mutex};
        session = m_shared->session;
    }
    if (session) {
        sendRepeat(session, mode);
    }
}

}  // namespace threnody::media
