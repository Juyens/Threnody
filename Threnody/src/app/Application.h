#pragma once

#include "audio/ProcessLoopbackCapture.h"
#include "audio/SpotifyVolume.h"
#include "dsp/BeatDetector.h"
#include "dsp/SpectrumAnalyzer.h"
#include "interaction/HitTest.h"
#include "media/MediaSession.h"
#include "overlay/KeyboardHook.h"
#include "overlay/LockKeyOverlay.h"
#include "overlay/VolumeFlyout.h"
#include "render/LayeredSurface.h"
#include "render/WidgetLayout.h"
#include "render/WidgetModel.h"
#include "render/WidgetRenderer.h"
#include "settings/Settings.h"
#include "shell/SpotifyWindow.h"
#include "spotify/SpotifyClient.h"
#include "taskbar/DockPreview.h"
#include "taskbar/RegistryWatcher.h"
#include "taskbar/Taskbar.h"
#include "taskbar/WidgetWindow.h"
#include "tray/SettingsWindow.h"
#include "tray/TrayIcon.h"
#include "util/Win32.h"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

namespace threnody {

// Owns the message loop and wires the pieces together: a hidden top-level
// window receives broadcasts (TaskbarCreated), timer ticks, registry-change
// and media-change notifications, and reacts by (re)embedding, moving or
// repainting the widget. Also decides when audio capture runs, drives the
// visualiser frames, and dispatches clicks to their actions.
class Application {
public:
    Application(HINSTANCE instance, std::filesystem::path dataDirectory);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs until the message window is closed. Returns the exit code.
    [[nodiscard]] int run();

private:
    static LRESULT CALLBACK messageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // `force` relayouts even when the taskbar itself did not change, for
    // content changes that alter the widget width.
    void syncWithTaskbar(bool force);
    void repaintWidget();

    // Dragging the widget out of the taskbar and back. The drag runs on a
    // timer that follows the cursor and the button state, so it does not
    // depend on mouse capture surviving the window being recreated.
    void beginDrag(POINT grab);
    void onDragFrame();
    void endDrag();
    void syncFloating();
    [[nodiscard]] float floatingHeightDip() const;
    [[nodiscard]] std::optional<RECT> dockSlot() const;
    void onMediaChanged();
    void setCover(std::vector<std::uint8_t> image);
    bool applyArtworkFallback();
    void refreshStaleLinks();
    void updateAccentFromCover();
    void onWidgetClick(POINT position);
    void onPointerMove(POINT position);
    void onPointerLeave();
    void setHoverFading(bool fading);
    void onHoverFrame();
    void openTrackOrArtist(bool artist);

    // Shuffle goes through SMTC; volume is Spotify's mixer session, set from
    // the flyout and polled on the health tick.
    void toggleShuffle();
    void syncShuffle(const std::optional<bool>& reported);
    void toggleVolumeFlyout();
    void refreshVolume();
    void showVolume(const std::optional<audio::VolumeState>& state);
    void toggleColorMode();
    void saveSettings();

    // Tray icon, its menu, and the settings window it opens.
    void onTrayEvent(WPARAM wParam, LPARAM lParam);
    void openSettings();
    void applySettings(const settings::Settings& updated);
    void applyLanguage();
    [[nodiscard]] const i18n::Strings& strings() const noexcept { return i18n::strings(m_settings.language); }
    void testOverlay();
    void connectSpotify(std::string clientId);
    void disconnectSpotify();
    void onSpotifyChanged();
    void publishSpotifyStatus();
    [[nodiscard]] bool linksMatchCurrentTrack() const;
    void quit();

    // Lock-key overlay: the hook exists only while the feature is enabled.
    void applyLockKeySettings();
    void onLockKey(overlay::LockKey key, bool on);

    // Audio capture follows the Spotify session: started when it exists,
    // restarted when Spotify's root process changes, retried after failures.
    void manageCapture();
    void setSpectrumRunning(bool running);
    void onSpectrumFrame();

    // Play/pause as shown: SMTC's status arrives 5-14 s late from Spotify, so
    // while audio is being captured the presence of signal decides.
    void setAudioWatch(bool running);
    void onAudioTick();
    void updatePlayingState();

    HINSTANCE m_instance{};
    std::filesystem::path m_dataDirectory;
    settings::Settings m_settings;

    win32::WindowClass m_messageClass;
    win32::unique_hwnd m_messageWindow;
    UINT m_taskbarCreatedMessage{};

    taskbar::WidgetWindow m_widget;
    render::LayeredSurface m_surface;
    std::unique_ptr<render::WidgetRenderer> m_renderer;
    render::WidgetModel m_model;
    render::WidgetLayout m_widgetLayout{};

    std::unique_ptr<media::MediaSession> m_media;
    bool m_sessionAvailable{false};
    std::uint32_t m_smtcCoverVersion{};  // Last NowPlaying::coverVersion applied.
    bool m_smtcCoverSettled{false};      // SMTC has answered for the current track's artwork.
    shell::SpotifyWindowToggle m_spotifyWindow;

    std::unique_ptr<spotify::SpotifyClient> m_spotify;
    spotify::Credentials m_savedCredentials;
    std::optional<spotify::TrackLinks> m_links;
    unsigned m_linksRetries{};
    std::wstring m_artworkRequested;  // Fallback URL already asked for this track.

    audio::ProcessLoopbackCapture m_capture;
    ULONGLONG m_lastCaptureAttempt{};
    bool m_captureFailureLogged{false};
    bool m_captureRunningLogged{false};
    dsp::SpectrumAnalyzer m_analyzer;
    dsp::BeatDetector m_beat;
    bool m_animating{false};  // The renderer has motion of its own; frames run.
    std::array<float, dsp::SpectrumAnalyzer::fftSize> m_frame{};
    bool m_spectrumRunning{false};
    bool m_smtcPlaying{false};
    bool m_audioWatch{false};
    bool m_audioSeen{false};
    bool m_audioFlowing{false};
    ULONGLONG m_lastTextChangeTick{};
    ULONGLONG m_audioIgnoreUntilTick{};  // Spotify fades out on pause; that tail is not "playing".
    std::uint64_t m_lastWritten{};
    ULONGLONG m_lastAudioActiveTick{};
    bool m_hoverFading{false};
    ULONGLONG m_hoverFrameTick{};

    std::unique_ptr<overlay::LockKeyOverlay> m_lockOverlay;
    std::unique_ptr<overlay::VolumeFlyout> m_volumeFlyout;  // Created on first use.
    std::unique_ptr<taskbar::DockPreview> m_dockPreview;    // Created on first drag.
    struct Drag {
        POINT grab{};  // Cursor offset inside the widget, pixels.
        bool overDock{false};
    };
    std::optional<Drag> m_drag;
    UINT m_widgetDpi{96};  // The widget's monitor; the taskbar's while docked.
    BYTE m_widgetAlpha{255};
    audio::SpotifyVolume m_volume;
    ULONGLONG m_shuffleHoldUntil{};
    std::unique_ptr<overlay::KeyboardHook> m_keyboardHook;
    bool m_overlayTestState{false};

    win32::unique_hicon m_trayIconImage;
    win32::unique_hicon m_appIconImage;
    std::unique_ptr<tray::TrayIcon> m_tray;
    std::unique_ptr<tray::SettingsWindow> m_settingsWindow;

    std::unique_ptr<taskbar::RegistryWatcher> m_alignmentWatcher;
    std::optional<taskbar::Layout> m_layout;
    RECT m_widgetRect{};
};

}  // namespace threnody
