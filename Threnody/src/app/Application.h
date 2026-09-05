#pragma once

#include "audio/ProcessLoopbackCapture.h"
#include "dsp/SpectrumAnalyzer.h"
#include "interaction/HitTest.h"
#include "media/MediaSession.h"
#include "overlay/KeyboardHook.h"
#include "overlay/LockKeyOverlay.h"
#include "render/LayeredSurface.h"
#include "render/WidgetLayout.h"
#include "render/WidgetModel.h"
#include "render/WidgetRenderer.h"
#include "settings/Settings.h"
#include "shell/SpotifyWindow.h"
#include "spotify/SpotifyClient.h"
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
    void onMediaChanged();
    void updateAccentFromCover();
    void onWidgetClick(POINT position);
    void onPointerMove(POINT position);
    void onPointerLeave();
    void setHoverFading(bool fading);
    void onHoverFrame();
    void openTrackOrArtist(bool artist);
    void toggleColorMode();
    void saveSettings();

    // Tray icon, its menu, and the settings window it opens.
    void onTrayEvent(WPARAM wParam, LPARAM lParam);
    void openSettings();
    void applySettings(const settings::Settings& updated);
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
    shell::SpotifyWindowToggle m_spotifyWindow;

    std::unique_ptr<spotify::SpotifyClient> m_spotify;
    spotify::Credentials m_savedCredentials;
    std::optional<spotify::TrackLinks> m_links;

    audio::ProcessLoopbackCapture m_capture;
    ULONGLONG m_lastCaptureAttempt{};
    bool m_captureFailureLogged{false};
    bool m_captureRunningLogged{false};
    dsp::SpectrumAnalyzer m_analyzer;
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
