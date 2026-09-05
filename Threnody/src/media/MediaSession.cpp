#include "media/MediaSession.h"

#include "media/SourceAppId.h"
#include "util/Log.h"

#include <unknwn.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

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

    // Incremented per property refresh; a refresh that finishes after a newer
    // one started discards its result instead of overwriting fresher data.
    std::uint32_t propertiesGeneration{};

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
        {
            std::scoped_lock lock{shared->mutex};
            shared->state.playing = playing;
        }
        shared->notify();
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

    try {
        const auto properties = co_await session.TryGetMediaPropertiesAsync();
        std::wstring title{properties.Title()};
        std::wstring artist{properties.Artist()};

        std::vector<std::uint8_t> cover;
        if (const auto thumbnail = properties.Thumbnail()) {
            const auto stream = co_await thumbnail.OpenReadAsync();
            const std::uint64_t size = stream.Size();
            if (size > 0 && size < maxCoverBytes) {
                Buffer buffer{static_cast<std::uint32_t>(size)};
                const auto read = co_await stream.ReadAsync(buffer, static_cast<std::uint32_t>(size), InputStreamOptions::None);
                cover.assign(read.data(), read.data() + read.Length());
            }
        }

        {
            std::scoped_lock lock{shared->mutex};
            if (generation != shared->propertiesGeneration) {
                co_return;  // A newer refresh is in flight.
            }
            NowPlaying& state = shared->state;
            state.available = true;
            state.title = std::move(title);
            state.artist = std::move(artist);
            if (state.cover != cover) {
                state.cover = std::move(cover);
                ++state.coverVersion;
            }
        }
        shared->notify();
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
    {
        std::scoped_lock lock{shared->mutex};
        if (shared->session == chosen) {
            return;
        }
        if (shared->session) {
            shared->session.MediaPropertiesChanged(shared->propertiesChangedToken);
            shared->session.PlaybackInfoChanged(shared->playbackChangedToken);
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
        } else {
            shared->state = NowPlaying{.coverVersion = shared->state.coverVersion + 1};
        }
    }

    if (changed) {
        log::info("SMTC Spotify session {}", chosen ? "found" : "gone");
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
                    pickSession(s);
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

}  // namespace threnody::media
