#include "app/Application.h"

#include "Config.h"
#include "color/DominantColor.h"
#include "render/CoverSampler.h"
#include "render/IconFactory.h"
#include "shell/Fullscreen.h"
#include "shell/SpotifyLinks.h"
#include "shell/SpotifyProcess.h"
#include "shell/Startup.h"
#include "util/Dpapi.h"

#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include "util/Log.h"
#include "util/Text.h"

#include <cmath>

namespace threnody {
namespace {

constexpr wchar_t messageClassName[] = L"ThrenodyMessageWindow";

constexpr UINT_PTR healthTimerId = 1;
constexpr UINT_PTR spectrumTimerId = 2;
constexpr UINT_PTR hoverTimerId = 3;
constexpr unsigned hoverFrameMs = 16;
constexpr UINT_PTR audioTimerId = 4;
constexpr UINT WM_THRENODY_ALIGNMENT_CHANGED = WM_APP + 1;
constexpr UINT WM_THRENODY_MEDIA_CHANGED = WM_APP + 2;
constexpr UINT WM_THRENODY_LOCK_KEY = WM_APP + 3;  // wParam: LockKey, lParam: on
constexpr UINT WM_THRENODY_TRAY = WM_APP + 4;
constexpr UINT WM_THRENODY_SPOTIFY = WM_APP + 5;

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

constexpr UINT menuSettingsId = 1;
constexpr UINT menuQuitId = 2;
constexpr tray::MenuItem trayMenu[] = {
    {.id = menuSettingsId, .text = L"Ajustes…"},
    {.id = menuQuitId, .text = L"Salir", .separatorBefore = true},
};

constexpr const char* alignmentName(taskbar::Alignment alignment) noexcept {
    return alignment == taskbar::Alignment::Left ? "left" : "center";
}

constexpr float pixelsToDip(int px, UINT dpi) noexcept {
    return static_cast<float>(px) * 96.0f / static_cast<float>(dpi);
}

int dipToPixels(float dip, UINT dpi) noexcept {
    return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / 96.0f));
}

}  // namespace

Application::Application(HINSTANCE instance, std::filesystem::path dataDirectory)
    : m_instance(instance),
      m_dataDirectory(std::move(dataDirectory)),
      m_settings(settings::load(m_dataDirectory / settings::fileName)),
      m_messageClass(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .lpfnWndProc = &Application::messageProc,
          .hInstance = instance,
          .lpszClassName = messageClassName,
      }),
      m_widget(instance),
      m_analyzer(static_cast<int>(audio::ProcessLoopbackCapture::sampleRate)) {
    // A real (not message-only) top-level window, otherwise it would miss the
    // TaskbarCreated broadcast. Never shown.
    m_messageWindow.reset(CreateWindowExW(WS_EX_TOOLWINDOW, m_messageClass.name(), L"Threnody", WS_OVERLAPPED,
                                          0, 0, 0, 0, nullptr, nullptr, instance, this));
    if (!m_messageWindow) {
        log::error("{}", Error::fromLastError("CreateWindowEx(ThrenodyMessageWindow)").describe());
        return;
    }
    const HWND messageWindow = m_messageWindow.get();

    if (Result<std::unique_ptr<render::WidgetRenderer>> renderer = render::WidgetRenderer::create(); renderer) {
        m_renderer = std::move(renderer.value());
    } else {
        log::error("renderer unavailable: {}", renderer.error().describe());
    }

    m_model.title = config::placeholderTitle;
    m_model.artist = config::placeholderArtist;
    m_model.colorMode = m_settings.colorMode;

    if (Result<std::unique_ptr<overlay::LockKeyOverlay>> lockOverlay = overlay::LockKeyOverlay::create(instance);
        lockOverlay) {
        m_lockOverlay = std::move(lockOverlay.value());
    } else {
        log::error("lock-key overlay unavailable: {}", lockOverlay.error().describe());
    }
    applyLockKeySettings();

    m_widget.onClick([this](POINT position) { onWidgetClick(position); });
    m_widget.onPointerMove([this](POINT position) { onPointerMove(position); });
    m_widget.onPointerLeave([this] { onPointerLeave(); });

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    m_alignmentWatcher = std::make_unique<taskbar::RegistryWatcher>(
        HKEY_CURRENT_USER, taskbar::explorerAdvancedKey, messageWindow, WM_THRENODY_ALIGNMENT_CHANGED);

    // Media events arrive on thread-pool threads; bounce them to this thread.
    m_media = std::make_unique<media::MediaSession>(
        [messageWindow] { PostMessageW(messageWindow, WM_THRENODY_MEDIA_CHANGED, 0, 0); });

    m_spotify = std::make_unique<spotify::SpotifyClient>(
        [messageWindow] { PostMessageW(messageWindow, WM_THRENODY_SPOTIFY, 0, 0); });
    if (!m_settings.spotifyClientId.empty() && !m_settings.spotifyRefreshTokenProtected.empty()) {
        if (Result<std::string> token = dpapi::unprotect(m_settings.spotifyRefreshTokenProtected); token) {
            m_savedCredentials = {.clientId = m_settings.spotifyClientId, .refreshToken = std::move(token.value())};
            m_spotify->setCredentials(m_savedCredentials);
            log::info("Spotify Web API credentials restored");
        } else {
            log::warn("Spotify refresh token unreadable, reconnect needed: {}", token.error().describe());
        }
    }

    if (m_renderer) {
        const UINT dpi = GetDpiForSystem();
        if (Result<win32::unique_hicon> icon =
                render::renderAppIcon(m_renderer->graphics(), GetSystemMetricsForDpi(SM_CXSMICON, dpi));
            icon) {
            m_trayIconImage = std::move(icon.value());
        } else {
            log::warn("tray icon image unavailable: {}", icon.error().describe());
        }
        if (Result<win32::unique_hicon> icon =
                render::renderAppIcon(m_renderer->graphics(), GetSystemMetricsForDpi(SM_CXICON, dpi));
            icon) {
            m_appIconImage = std::move(icon.value());
        }
    }
    m_tray = std::make_unique<tray::TrayIcon>(messageWindow, WM_THRENODY_TRAY, m_trayIconImage.get(), L"Threnody");

    m_settingsWindow = std::make_unique<tray::SettingsWindow>(
        instance, tray::SettingsActions{
                      .onChanged = [this](const settings::Settings& updated) { applySettings(updated); },
                      .onTestOverlay = [this] { testOverlay(); },
                      .onConnectSpotify = [this](std::string clientId) { connectSpotify(std::move(clientId)); },
                      .onDisconnectSpotify = [this] { disconnectSpotify(); },
                      .onQuit = [this] { quit(); },
                  });

    // Keep the Run entry pointing at wherever the executable lives now.
    if (m_settings.startWithWindows) {
        if (const Result<void> set = shell::setStartWithWindows(true); !set) {
            log::warn("{}", set.error().describe());
        }
    }

    SetTimer(messageWindow, healthTimerId, config::taskbarHealthCheckMs, nullptr);
    syncWithTaskbar(true);

    if (!m_settings.setupShown) {
        m_settings.setupShown = true;
        saveSettings();
        openSettings();
    }
}

Application::~Application() {
    // These post to the message window; stop them before the window goes.
    m_spotify.reset();
    m_media.reset();
    m_alignmentWatcher.reset();
    m_capture.stop();
    m_settingsWindow.reset();
    m_tray.reset();
}

int Application::run() {
    if (!m_messageWindow) {
        return EXIT_FAILURE;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK Application::messageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT Application::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == m_taskbarCreatedMessage && m_taskbarCreatedMessage != 0) {
        log::info("taskbar created; re-embedding");
        m_layout.reset();
        syncWithTaskbar(true);
        if (m_tray) {
            m_tray->readd();
        }
        return 0;
    }

    switch (message) {
        case WM_THRENODY_ALIGNMENT_CHANGED:
            syncWithTaskbar(false);
            return 0;

        case WM_TIMER:
            if (wParam == healthTimerId) {
                syncWithTaskbar(false);
                manageCapture();
                if (m_media) {
                    m_media->poll();
                }
            } else if (wParam == spectrumTimerId) {
                onSpectrumFrame();
            } else if (wParam == hoverTimerId) {
                onHoverFrame();
            } else if (wParam == audioTimerId) {
                onAudioTick();
            }
            return 0;

        case WM_THRENODY_MEDIA_CHANGED:
            onMediaChanged();
            return 0;

        case WM_THRENODY_LOCK_KEY:
            onLockKey(static_cast<overlay::LockKey>(wParam), lParam != 0);
            return 0;

        case WM_THRENODY_TRAY:
            onTrayEvent(wParam, lParam);
            return 0;

        case WM_THRENODY_SPOTIFY:
            onSpotifyChanged();
            return 0;

        case WM_ENDSESSION:
            if (wParam) {
                log::info("exit: session ending");
                log::flush();
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, healthTimerId);
            KillTimer(hwnd, spectrumTimerId);
            KillTimer(hwnd, hoverTimerId);
            KillTimer(hwnd, audioTimerId);
            PostQuitMessage(EXIT_SUCCESS);
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (m_messageWindow.get() == hwnd) {
                m_messageWindow.release();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Brings the widget in line with the taskbar's current state. Cheap enough to
// run on every timer tick: a handful of FindWindow/GetWindowRect calls.
void Application::syncWithTaskbar(bool force) {
    const std::optional<taskbar::Layout> current = taskbar::queryLayout();
    if (!current) {
        if (m_layout) {
            log::warn("taskbar not found; waiting for it to come back");
            m_layout.reset();
        }
        return;
    }

    const bool embedded = m_widget.isEmbeddedIn(current->taskbar);
    const bool layoutChanged = !m_layout || !m_layout->sameGeometry(*current);
    if (embedded && !layoutChanged && !force) {
        return;
    }

    if (!m_layout || m_layout->alignment != current->alignment) {
        log::info("taskbar alignment: {}", alignmentName(current->alignment));
    }

    // Height follows the taskbar; width follows the content.
    const int heightPx =
        win32::height(current->bounds) - 2 * win32::scaleDip(config::widgetVerticalMarginDip, current->dpi);
    int widthPx = win32::scaleDip(config::widgetMaxWidthDip, current->dpi) / 2;
    if (m_renderer) {
        if (Result<render::WidgetLayout> widgetLayout =
                m_renderer->layout(m_model, pixelsToDip(heightPx, current->dpi));
            widgetLayout) {
            m_widgetLayout = widgetLayout.value();
            widthPx = dipToPixels(m_widgetLayout.width, current->dpi);
        } else {
            log::error("{}", widgetLayout.error().describe());
        }
    }
    const RECT rect = taskbar::placeWidget(*current, widthPx);

    if (!embedded) {
        if (const Result<void> result = m_widget.embed(current->taskbar, rect); !result) {
            log::error("{}", result.error().describe());
            return;
        }
        log::info("widget embedded at ({}, {}) {}x{} px, dpi {}", rect.left, rect.top, win32::width(rect),
                  win32::height(rect), current->dpi);
    } else if (!win32::sameRect(rect, m_widgetRect)) {
        m_widget.move(rect);
    }

    m_widgetRect = rect;
    m_layout = current;
    repaintWidget();
}

void Application::repaintWidget() {
    if (!m_renderer || !m_layout) {
        return;
    }

    const SIZE size{.cx = win32::width(m_widgetRect), .cy = win32::height(m_widgetRect)};
    if (const Result<void> resized = m_surface.resize(size); !resized) {
        log::error("{}", resized.error().describe());
        return;
    }

    if (const Result<void> drawn = m_renderer->draw(m_surface, m_model, m_widgetLayout, m_layout->dpi); !drawn) {
        log::error("{}", drawn.error().describe());
        return;
    }

    if (const Result<void> presented = m_surface.present(m_widget.hwnd()); !presented) {
        log::error("{}", presented.error().describe());
        return;
    }
    m_widget.show();
}

void Application::onMediaChanged() {
    const media::NowPlaying now = m_media->snapshot();

    const bool textChanged = now.available ? (m_model.title != now.title || m_model.artist != now.artist)
                                           : (m_model.title != config::placeholderTitle);
    const bool sessionChanged = m_sessionAvailable != now.available;
    const bool coverChanged = m_model.coverVersion != now.coverVersion;
    m_sessionAvailable = now.available;
    const bool smtcPlaying = now.available && now.playing;
    if (smtcPlaying != m_smtcPlaying) {
        // Take SMTC's word right away when it does speak (sometimes it is
        // quick); the audio watch confirms or corrects within its hold.
        m_smtcPlaying = smtcPlaying;
        m_lastAudioActiveTick = smtcPlaying ? GetTickCount64() : 0;
        if (!smtcPlaying) {
            m_audioIgnoreUntilTick = GetTickCount64() + config::audioPauseGraceMs;
        }
        if (smtcPlaying != m_model.playing) {
            m_model.playing = smtcPlaying;
            log::info("playback shown as {} (smtc)", smtcPlaying ? "playing" : "paused");
            if (smtcPlaying) {
                setSpectrumRunning(true);
            }
        }
    }

    if (now.available) {
        m_model.title = now.title;
        m_model.artist = now.artist;
        if (now.coverPending) {
            // New track, old artwork: show the placeholder until the new one lands.
            if (!m_model.coverImage.empty()) {
                m_model.coverImage.clear();
                m_model.accent = config::defaultAccentColor;
            }
        } else if (coverChanged) {
            m_model.coverImage = now.cover;
            m_model.coverVersion = now.coverVersion;
            updateAccentFromCover();
        }
    } else {
        m_model.title = config::placeholderTitle;
        m_model.artist = config::placeholderArtist;
        m_model.coverImage.clear();
        m_model.coverVersion = now.coverVersion;
        m_model.accent = config::defaultAccentColor;
    }

    if (textChanged) {
        m_lastTextChangeTick = GetTickCount64();
        log::info("now playing: {} / {}", text::toUtf8(m_model.title), text::toUtf8(m_model.artist));
    }
    if (sessionChanged) {
        manageCapture();
    }
    updatePlayingState();
    if (textChanged) {
        // Exact links belong to the previous track until the API answers.
        m_links.reset();
        if (now.available && m_spotify && m_spotify->connected()) {
            m_spotify->requestNowPlaying();
        }
        syncWithTaskbar(true);  // Width may change with the text.
    } else {
        repaintWidget();
    }
}

void Application::updateAccentFromCover() {
    m_model.accent = config::defaultAccentColor;
    if (m_model.coverImage.empty() || !m_renderer) {
        return;
    }
    Result<std::vector<std::uint32_t>> pixels =
        render::sampleCover(m_renderer->wic(), m_model.coverImage, config::coverSampleSize);
    if (!pixels) {
        log::warn("cover colour analysis skipped: {}", pixels.error().describe());
        return;
    }
    m_model.accent = color::dominantColor(*pixels, config::defaultAccentColor);
    log::info("cover accent: rgb({:.0f}, {:.0f}, {:.0f})", m_model.accent.r * 255.0f, m_model.accent.g * 255.0f,
              m_model.accent.b * 255.0f);
}

void Application::onWidgetClick(POINT position) {
    if (!m_layout) {
        return;
    }
    const float x = pixelsToDip(position.x, m_layout->dpi);
    const float y = pixelsToDip(position.y, m_layout->dpi);

    const interaction::Zone zone = interaction::hitTest(m_widgetLayout, x, y);
    log::info("click at ({:.0f}, {:.0f}) dip -> zone {}", x, y, static_cast<int>(zone));

    switch (zone) {
        case interaction::Zone::Background:
        case interaction::Zone::Cover:
            m_spotifyWindow.toggle();
            break;
        case interaction::Zone::Title:
            openTrackOrArtist(false);
            break;
        case interaction::Zone::Artist:
            openTrackOrArtist(true);
            break;
        case interaction::Zone::Previous:
            m_media->send(media::TransportCommand::Previous);
            m_lastTextChangeTick = GetTickCount64();  // A loading gap is coming; do not read it as a pause.
            break;
        case interaction::Zone::PlayPause:
            m_media->send(media::TransportCommand::TogglePlayPause);
            // Spotify takes several seconds to report the new state through
            // SMTC; flip the glyph now and let the audio confirm it.
            m_model.playing = !m_model.playing;
            m_smtcPlaying = m_model.playing;
            m_lastAudioActiveTick = m_model.playing ? GetTickCount64() : 0;
            if (!m_model.playing) {
                m_audioIgnoreUntilTick = GetTickCount64() + config::audioPauseGraceMs;
            }
            if (m_model.playing) {
                setSpectrumRunning(true);
            }
            log::info("playback shown as {} (click)", m_model.playing ? "playing" : "paused");
            repaintWidget();
            break;
        case interaction::Zone::Next:
            m_media->send(media::TransportCommand::Next);
            m_lastTextChangeTick = GetTickCount64();
            break;
        case interaction::Zone::Visualizer:
            toggleColorMode();
            break;
    }
}

// Title or artist click. Opening a spotify: link raises Spotify, so the
// window toggle is told about it; otherwise the next cover click would try to
// raise an already-raised window instead of minimising it.
void Application::openTrackOrArtist(bool artist) {
    const bool exact = linksMatchCurrentTrack() && !(artist ? m_links->artistUri : m_links->trackUri).empty();
    if (exact) {
        shell::openSpotifyUri(artist ? m_links->artistUri : m_links->trackUri);
    } else if (m_sessionAvailable && !(artist ? m_model.artist : m_model.title).empty()) {
        if (artist) {
            shell::openSpotifySearch(m_model.artist);
        } else {
            shell::openSpotifySearch(m_model.artist.empty() ? m_model.title : m_model.artist + L" " + m_model.title);
        }
    } else {
        m_spotifyWindow.toggle();
        return;
    }
    m_spotifyWindow.assumeSpotifyInFront();
}

void Application::onPointerMove(POINT position) {
    // Re-read the foreground on every move: Spotify may have come to the
    // front (a link, a media key) while the pointer stayed on the widget.
    m_spotifyWindow.rememberForeground();
    if (!m_layout) {
        return;
    }
    const std::optional<render::Zone> zone = interaction::hitTest(
        m_widgetLayout, pixelsToDip(position.x, m_layout->dpi), pixelsToDip(position.y, m_layout->dpi));
    if (zone != m_model.hover) {
        m_model.hover = zone;
        repaintWidget();
    }
    setHoverFading(true);
}

void Application::onPointerLeave() {
    m_model.hover.reset();
    setHoverFading(true);
    repaintWidget();
}

void Application::setHoverFading(bool fading) {
    if (fading == m_hoverFading || !m_messageWindow) {
        return;
    }
    m_hoverFading = fading;
    if (fading) {
        m_hoverFrameTick = GetTickCount64();
        SetTimer(m_messageWindow.get(), hoverTimerId, hoverFrameMs, nullptr);
    } else {
        KillTimer(m_messageWindow.get(), hoverTimerId);
    }
}

// Moves the hover fade toward its target (1 while the pointer is over the
// widget, 0 after it leaves) and stops the timer once it gets there.
void Application::onHoverFrame() {
    const ULONGLONG now = GetTickCount64();
    const float step = static_cast<float>(now - m_hoverFrameTick) / static_cast<float>(config::hoverFadeMs);
    m_hoverFrameTick = now;
    const float target = m_model.hover ? 1.0f : 0.0f;
    const float before = m_model.hoverProgress;
    m_model.hoverProgress = target > before ? std::min(target, before + step) : std::max(target, before - step);
    if (m_model.hoverProgress != before) {
        repaintWidget();
    }
    if (m_model.hoverProgress == target) {
        setHoverFading(false);
    }
}

void Application::toggleColorMode() {
    m_settings.colorMode = m_settings.colorMode == ColorMode::Track ? ColorMode::Rainbow : ColorMode::Track;
    m_model.colorMode = m_settings.colorMode;
    log::info("colour mode: {}", m_model.colorMode == ColorMode::Rainbow ? "rainbow" : "track");
    saveSettings();
    if (m_settingsWindow) {
        m_settingsWindow->setSettings(m_settings);
    }
    repaintWidget();
}

void Application::onTrayEvent(WPARAM wParam, LPARAM lParam) {
    const UINT event = LOWORD(lParam);
    switch (event) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            openSettings();
            break;
        case WM_CONTEXTMENU: {
            const POINT anchor{.x = GET_X_LPARAM(wParam), .y = GET_Y_LPARAM(wParam)};
            switch (m_tray ? m_tray->showMenu(trayMenu, anchor) : 0) {
                case menuSettingsId: openSettings(); break;
                case menuQuitId: quit(); break;
                default: break;
            }
            break;
        }
        default:
            break;
    }
}

void Application::openSettings() {
    if (!m_settingsWindow) {
        return;
    }
    if (const Result<void> opened = m_settingsWindow->open(m_settings, m_appIconImage.get()); !opened) {
        log::error("settings window: {}", opened.error().describe());
        return;
    }
    publishSpotifyStatus();
}

// Every change from the settings window lands here and takes effect at once.
void Application::applySettings(const settings::Settings& updated) {
    const settings::Settings previous = m_settings;
    m_settings = updated;

    if (previous.colorMode != updated.colorMode) {
        m_model.colorMode = updated.colorMode;
        log::info("colour mode: {}", m_model.colorMode == ColorMode::Rainbow ? "rainbow" : "track");
        repaintWidget();
    }
    if (previous.startWithWindows != updated.startWithWindows) {
        if (const Result<void> set = shell::setStartWithWindows(updated.startWithWindows); set) {
            log::info("start with Windows: {}", updated.startWithWindows ? "on" : "off");
        } else {
            log::error("{}", set.error().describe());
        }
    }
    applyLockKeySettings();
    saveSettings();
}

void Application::testOverlay() {
    if (m_lockOverlay) {
        m_overlayTestState = !m_overlayTestState;
        m_lockOverlay->show(overlay::LockKey::CapsLock, m_overlayTestState);
    }
}

void Application::connectSpotify(std::string clientId) {
    m_settings.spotifyClientId = clientId;
    saveSettings();
    if (m_spotify) {
        m_spotify->beginAuthorization(std::move(clientId));
    }
}

void Application::disconnectSpotify() {
    if (m_spotify) {
        m_spotify->disconnect();
    }
    m_savedCredentials = {};
    m_links.reset();
    m_settings.spotifyRefreshTokenProtected.clear();
    saveSettings();
    publishSpotifyStatus();
}

// The Web API client changed state or answered: persist rotated credentials,
// pick up exact links, and keep the settings window informed.
void Application::onSpotifyChanged() {
    if (!m_spotify) {
        return;
    }
    if (m_spotify->connected()) {
        const spotify::Credentials current = m_spotify->credentials();
        if (current != m_savedCredentials && !current.refreshToken.empty()) {
            if (Result<std::string> sealed = dpapi::protect(current.refreshToken); sealed) {
                m_settings.spotifyClientId = current.clientId;
                m_settings.spotifyRefreshTokenProtected = std::move(sealed.value());
                m_savedCredentials = current;
                saveSettings();
            } else {
                log::error("{}", sealed.error().describe());
            }
        }
        if (!m_links && m_sessionAvailable) {
            const std::optional<spotify::TrackLinks> links = m_spotify->links();
            if (links) {
                m_links = links;
                log::info("Spotify links: {} -> {}", text::toUtf8(links->trackName), text::toUtf8(links->trackUri));
            } else {
                m_spotify->requestNowPlaying();
            }
        }
    }
    publishSpotifyStatus();
}

void Application::publishSpotifyStatus() {
    if (!m_settingsWindow || !m_spotify) {
        return;
    }
    const spotify::Status status = m_spotify->status();
    m_settingsWindow->setSpotifyStatus(
        {.connected = status.state == spotify::AuthState::Connected, .detail = status.detail});
}

bool Application::linksMatchCurrentTrack() const {
    return m_links && m_sessionAvailable && equalsIgnoreCase(m_links->trackName, m_model.title);
}

void Application::quit() {
    if (m_messageWindow) {
        log::info("quit requested from the tray");
        PostMessageW(m_messageWindow.get(), WM_CLOSE, 0, 0);
    }
}

void Application::saveSettings() {
    if (const Result<void> saved = settings::save(m_settings, m_dataDirectory / settings::fileName); !saved) {
        log::error("{}", saved.error().describe());
    }
}

void Application::applyLockKeySettings() {
    const bool wanted = m_settings.lockKeys.enabled && m_lockOverlay != nullptr;
    if (wanted && !m_keyboardHook) {
        const HWND messageWindow = m_messageWindow.get();
        // The hook runs on this thread, but a posted message keeps the hook
        // procedure itself trivial; Windows unhooks callbacks that dawdle.
        m_keyboardHook = std::make_unique<overlay::KeyboardHook>([messageWindow](overlay::LockKey key, bool on) {
            PostMessageW(messageWindow, WM_THRENODY_LOCK_KEY, static_cast<WPARAM>(key), on ? 1 : 0);
        });
        if (!m_keyboardHook->installed()) {
            m_keyboardHook.reset();
        } else {
            log::info("lock-key overlay enabled");
        }
    } else if (!wanted && m_keyboardHook) {
        m_keyboardHook.reset();
        log::info("lock-key overlay disabled");
    }
}

void Application::onLockKey(overlay::LockKey key, bool on) {
    if (!m_lockOverlay) {
        return;
    }
    const settings::LockKeyOverlay& keys = m_settings.lockKeys;
    bool enabled = false;
    switch (key) {
        case overlay::LockKey::CapsLock: enabled = keys.capsLock; break;
        case overlay::LockKey::NumLock: enabled = keys.numLock; break;
        case overlay::LockKey::ScrollLock: enabled = keys.scrollLock; break;
        case overlay::LockKey::Insert: enabled = keys.insert; break;
    }
    if (!enabled || shell::isFullscreenApplicationInFront()) {
        return;
    }
    m_lockOverlay->show(key, on);
}

void Application::manageCapture() {
    using audio::CaptureStatus;
    const CaptureStatus status = m_capture.status();

    if (!m_sessionAvailable) {
        if (status != CaptureStatus::Stopped) {
            log::info("audio capture stopped: Spotify session gone");
            m_capture.stop();
        }
        return;
    }

    if (status == CaptureStatus::Failed && !m_captureFailureLogged) {
        log::error("audio capture failed, visualiser disabled until retry: {}", m_capture.failure());
        m_captureFailureLogged = true;
    }
    if (status == CaptureStatus::Running && !m_captureRunningLogged) {
        log::info("audio capture running (pid {})", m_capture.processId());
        m_captureRunningLogged = true;
    }
    if (status != CaptureStatus::Running) {
        m_captureRunningLogged = false;
    }
    setAudioWatch(status == CaptureStatus::Running);

    const std::optional<shell::SpotifyProcess> spotify = shell::findSpotify();
    if (!spotify) {
        if (status == CaptureStatus::Running || status == CaptureStatus::Starting) {
            log::info("audio capture stopped: no Spotify process");
            m_capture.stop();
        }
        return;
    }

    if (status == CaptureStatus::Running || status == CaptureStatus::Starting) {
        if (m_capture.processId() != spotify->processId) {
            log::info("Spotify root process changed ({} -> {}); restarting capture", m_capture.processId(),
                      spotify->processId);
            m_capture.stop();
        } else {
            return;
        }
    }

    const ULONGLONG now = GetTickCount64();
    if (status != CaptureStatus::Stopped && now - m_lastCaptureAttempt < config::captureRetryMs) {
        return;
    }
    m_lastCaptureAttempt = now;
    m_captureFailureLogged = false;
    log::info("starting audio capture of Spotify pid {} ({} main window)", spotify->processId,
              spotify->mainWindow != nullptr ? "with" : "no");
    m_capture.start(spotify->processId);
}

void Application::setSpectrumRunning(bool running) {
    if (running == m_spectrumRunning || !m_messageWindow) {
        return;
    }
    m_spectrumRunning = running;
    if (running) {
        SetTimer(m_messageWindow.get(), spectrumTimerId, config::spectrumFrameMs, nullptr);
    } else {
        KillTimer(m_messageWindow.get(), spectrumTimerId);
    }
}

void Application::setAudioWatch(bool running) {
    if (running == m_audioWatch || !m_messageWindow) {
        return;
    }
    m_audioWatch = running;
    if (running) {
        m_audioSeen = false;
        m_lastWritten = m_capture.samples().totalWritten();
        SetTimer(m_messageWindow.get(), audioTimerId, config::audioWatchMs, nullptr);
    } else {
        KillTimer(m_messageWindow.get(), audioTimerId);
        updatePlayingState();
    }
}

// Looks at the captured audio: new samples above the silence floor mean
// Spotify is playing, whatever SMTC still says. Stops delivering samples, or
// delivers silence, for the hold time and it is paused.
void Application::onAudioTick() {
    if (m_capture.status() != audio::CaptureStatus::Running) {
        setAudioWatch(false);
        return;
    }
    const std::uint64_t written = m_capture.samples().totalWritten();
    const bool advanced = written != m_lastWritten;
    m_lastWritten = written;
    if (advanced != m_audioFlowing) {
        m_audioFlowing = advanced;
        log::info("audio stream {}", advanced ? "resumed" : "stalled");
    }
    if (advanced) {
        std::array<float, 2048> recent{};
        m_capture.samples().latest(recent);
        double energy = 0.0;
        for (const float sample : recent) {
            energy += static_cast<double>(sample) * sample;
        }
        const double rms = std::sqrt(energy / static_cast<double>(recent.size()));
        if (rms > config::audioSilenceRms && GetTickCount64() >= m_audioIgnoreUntilTick) {
            m_lastAudioActiveTick = GetTickCount64();
            m_audioSeen = true;
        }
    }
    updatePlayingState();
}

// Decides what the play/pause glyph shows. With audio being captured, signal
// (or its absence for the hold time) wins; otherwise SMTC's status is all
// there is.
void Application::updatePlayingState() {
    bool playing = m_smtcPlaying;
    if (m_audioWatch && m_audioSeen) {
        const ULONGLONG now = GetTickCount64();
        const bool justChangedTrack = now - m_lastTextChangeTick < config::audioTrackChangeWindowMs;
        const unsigned hold = justChangedTrack ? config::audioTrackChangeHoldMs : config::audioPauseHoldMs;
        playing = now - m_lastAudioActiveTick < hold;
    }
    if (playing == m_model.playing) {
        return;
    }
    m_model.playing = playing;
    log::info("playback shown as {} ({})", playing ? "playing" : "paused", m_audioWatch && m_audioSeen ? "audio" : "smtc");
    if (playing) {
        setSpectrumRunning(true);
    }
    repaintWidget();
}

// One visualiser frame. Runs at ~30 fps only while playing, then keeps going
// just long enough for the bars to settle on the baseline. The rainbow
// gradient travels while frames run and stands still otherwise.
void Application::onSpectrumFrame() {
    if (m_model.playing && m_capture.status() == audio::CaptureStatus::Running) {
        m_capture.samples().latest(m_frame);
        m_analyzer.analyze(m_frame);
    } else {
        m_analyzer.decay();
    }
    m_model.spectrum = m_analyzer.bands();

    if (m_model.colorMode == ColorMode::Rainbow) {
        const float step = static_cast<float>(config::spectrumFrameMs) / (1000.0f * config::rainbowCycleSeconds);
        m_model.rainbowPhase = std::fmod(m_model.rainbowPhase + step, 1.0f);
    }
    repaintWidget();

    if (!m_model.playing && m_analyzer.idle()) {
        setSpectrumRunning(false);
    }
}

}  // namespace threnody
