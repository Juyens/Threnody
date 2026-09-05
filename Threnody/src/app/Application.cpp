#include "app/Application.h"

#include "Config.h"
#include "shell/SpotifyProcess.h"
#include "util/Log.h"
#include "util/Text.h"

#include <cmath>

namespace threnody {
namespace {

constexpr wchar_t messageClassName[] = L"ThrenodyMessageWindow";

constexpr UINT_PTR healthTimerId = 1;
constexpr UINT_PTR spectrumTimerId = 2;
constexpr UINT WM_THRENODY_ALIGNMENT_CHANGED = WM_APP + 1;
constexpr UINT WM_THRENODY_MEDIA_CHANGED = WM_APP + 2;

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

Application::Application(HINSTANCE instance)
    : m_instance(instance),
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

    m_widget.onClick([this](POINT position) { onWidgetClick(position); });

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    m_alignmentWatcher = std::make_unique<taskbar::RegistryWatcher>(
        HKEY_CURRENT_USER, taskbar::explorerAdvancedKey, messageWindow, WM_THRENODY_ALIGNMENT_CHANGED);

    // Media events arrive on thread-pool threads; bounce them to this thread.
    m_media = std::make_unique<media::MediaSession>(
        [messageWindow] { PostMessageW(messageWindow, WM_THRENODY_MEDIA_CHANGED, 0, 0); });

    SetTimer(messageWindow, healthTimerId, config::taskbarHealthCheckMs, nullptr);
    syncWithTaskbar(true);
}

Application::~Application() {
    // Both post to the message window; stop them before the window goes.
    m_media.reset();
    m_alignmentWatcher.reset();
    m_capture.stop();
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
            } else if (wParam == spectrumTimerId) {
                onSpectrumFrame();
            }
            return 0;

        case WM_THRENODY_MEDIA_CHANGED:
            onMediaChanged();
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, healthTimerId);
            KillTimer(hwnd, spectrumTimerId);
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
    const bool playingChanged = m_model.playing != (now.available && now.playing);
    const bool sessionChanged = m_sessionAvailable != now.available;
    m_sessionAvailable = now.available;

    if (now.available) {
        m_model.title = now.title;
        m_model.artist = now.artist;
        m_model.playing = now.playing;
        if (m_model.coverVersion != now.coverVersion) {
            m_model.coverImage = now.cover;
            m_model.coverVersion = now.coverVersion;
        }
    } else {
        m_model.title = config::placeholderTitle;
        m_model.artist = config::placeholderArtist;
        m_model.playing = false;
        m_model.coverImage.clear();
        m_model.coverVersion = now.coverVersion;
    }

    if (textChanged || playingChanged) {
        log::info("now playing: {} / {} ({})", text::toUtf8(m_model.title), text::toUtf8(m_model.artist),
                  now.available ? (now.playing ? "playing" : "paused") : "no session");
    }
    if (sessionChanged) {
        manageCapture();
    }
    if (m_model.playing) {
        setSpectrumRunning(true);
    }
    if (textChanged) {
        syncWithTaskbar(true);  // Width may change with the text.
    } else {
        repaintWidget();
    }
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
        case interaction::Zone::Previous:
            m_media->send(media::TransportCommand::Previous);
            break;
        case interaction::Zone::PlayPause:
            m_media->send(media::TransportCommand::TogglePlayPause);
            // Spotify takes several seconds to report the new state through
            // SMTC; flip the glyph now and let the event confirm it.
            m_model.playing = !m_model.playing;
            if (m_model.playing) {
                setSpectrumRunning(true);
            }
            repaintWidget();
            break;
        case interaction::Zone::Next:
            m_media->send(media::TransportCommand::Next);
            break;
        default:
            break;  // Remaining zones arrive with the interaction phase.
    }
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

// One visualiser frame. Runs at ~30 fps only while playing, then keeps going
// just long enough for the bars to settle on the baseline.
void Application::onSpectrumFrame() {
    if (m_model.playing && m_capture.status() == audio::CaptureStatus::Running) {
        m_capture.samples().latest(m_frame);
        m_analyzer.analyze(m_frame);
    } else {
        m_analyzer.decay();
    }
    m_model.spectrum = m_analyzer.bands();
    repaintWidget();

    if (!m_model.playing && m_analyzer.idle()) {
        setSpectrumRunning(false);
    }
}

}  // namespace threnody
