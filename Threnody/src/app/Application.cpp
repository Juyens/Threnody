#include "app/Application.h"

#include "Config.h"
#include "color/DominantColor.h"
#include "render/CoverSampler.h"
#include "shell/Fullscreen.h"
#include "shell/SpotifyLinks.h"
#include "shell/SpotifyProcess.h"
#include "shell/Startup.h"
#include "util/Dpapi.h"

#include "Resource.h"

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
constexpr UINT_PTR dragTimerId = 5;
constexpr UINT_PTR animationTimerId = 6;
constexpr UINT_PTR peekTimerId = 7;
constexpr UINT_PTR queueRetryTimerId = 8;
constexpr UINT WM_THRENODY_ALIGNMENT_CHANGED = WM_APP + 1;
constexpr UINT WM_THRENODY_MEDIA_CHANGED = WM_APP + 2;
constexpr UINT WM_THRENODY_LOCK_KEY = WM_APP + 3;  // wParam: LockKey, lParam: on
constexpr UINT WM_THRENODY_TRAY = WM_APP + 4;
constexpr UINT WM_THRENODY_SPOTIFY = WM_APP + 5;
constexpr UINT WM_THRENODY_DRAG = WM_APP + 6;   // wParam, lParam: grab point in the widget
constexpr UINT WM_THRENODY_WHEEL = WM_APP + 7;  // wParam: wheel delta (signed)
constexpr UINT WM_THRENODY_RESIZE = WM_APP + 8;  // wParam: WidgetWindow::Edge mask

// Shifts `rect` fully onto the work area of the monitor it is (mostly) on,
// so a floating widget can never be left off-screen.
RECT keepOnScreen(RECT rect) {
    MONITORINFO monitor{.cbSize = sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return rect;
    }
    const RECT& work = monitor.rcWork;
    const LONG dx = rect.left < work.left ? work.left - rect.left : rect.right > work.right ? work.right - rect.right : 0;
    const LONG dy = rect.top < work.top ? work.top - rect.top : rect.bottom > work.bottom ? work.bottom - rect.bottom : 0;
    OffsetRect(&rect, dx, dy);
    return rect;
}

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

constexpr UINT menuSettingsId = 1;
constexpr UINT menuQuitId = 2;

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

    m_model.title = strings().placeholderTitle.wide;
    m_model.artist = strings().placeholderArtist.wide;
    m_model.colorMode = m_settings.colorMode;
    m_model.floating = m_settings.floating;

    if (Result<std::unique_ptr<overlay::LockKeyOverlay>> lockOverlay = overlay::LockKeyOverlay::create(instance);
        lockOverlay) {
        m_lockOverlay = std::move(lockOverlay.value());
        m_lockOverlay->setLanguage(m_settings.language);
    } else {
        log::error("lock-key overlay unavailable: {}", lockOverlay.error().describe());
    }
    applyLockKeySettings();

    m_widget.onClick([this](POINT position) { onWidgetClick(position); });
    m_widget.onPointerMove([this](POINT position) { onPointerMove(position); });
    m_widget.onPointerLeave([this] { onPointerLeave(); });
    // Posted: starting the drag recreates the window whose message is being handled.
    m_widget.onDragStart([messageWindow](POINT grab) {
        PostMessageW(messageWindow, WM_THRENODY_DRAG, static_cast<WPARAM>(grab.x), static_cast<LPARAM>(grab.y));
    });
    m_widget.onResizeStart([messageWindow](UINT edges) {
        PostMessageW(messageWindow, WM_THRENODY_RESIZE, static_cast<WPARAM>(edges), 0);
    });

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

    // The icon lives in the executable's resources so Explorer shows it too;
    // LoadImage picks the frame nearest each requested size.
    {
        const UINT dpi = GetDpiForSystem();
        const auto load = [&](int metric) {
            const int size = GetSystemMetricsForDpi(metric, dpi);
            return win32::unique_hicon{static_cast<HICON>(
                LoadImageW(instance, MAKEINTRESOURCEW(IDI_THRENODY), IMAGE_ICON, size, size, LR_DEFAULTCOLOR))};
        };
        m_trayIconImage = load(SM_CXSMICON);
        m_appIconImage = load(SM_CXICON);
        if (!m_trayIconImage || !m_appIconImage) {
            log::warn("application icon resource unavailable: {}", Error::fromLastError("LoadImage").describe());
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
                refreshStaleLinks();
                syncShuffle(m_media ? m_media->snapshot().shuffle : std::nullopt);
                refreshVolume();
            } else if (wParam == spectrumTimerId) {
                onSpectrumFrame();
            } else if (wParam == hoverTimerId) {
                onHoverFrame();
            } else if (wParam == audioTimerId) {
                onAudioTick();
            } else if (wParam == dragTimerId) {
                onDragFrame();
            } else if (wParam == animationTimerId) {
                repaintWidget();
            } else if (wParam == peekTimerId) {
                KillTimer(hwnd, peekTimerId);
            KillTimer(hwnd, queueRetryTimerId);
                updateQueuePeek();
            } else if (wParam == queueRetryTimerId) {
                KillTimer(hwnd, queueRetryTimerId);
                if (!m_upNextKnown && m_queueRequest == 0 && m_peekHoverSince != 0 && m_spotify) {
                    m_queueRequest = m_spotify->requestQueue();
                }
            }
            return 0;

        case WM_THRENODY_WHEEL:
            onWheel(static_cast<int>(static_cast<INT_PTR>(wParam)));
            return 0;

        case WM_THRENODY_RESIZE:
            beginResize(static_cast<UINT>(wParam));
            return 0;

        case WM_THRENODY_DRAG:
            beginDrag(POINT{.x = static_cast<LONG>(wParam), .y = static_cast<LONG>(lParam)});
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
            KillTimer(hwnd, dragTimerId);
            KillTimer(hwnd, animationTimerId);
            KillTimer(hwnd, peekTimerId);
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

    // Floating, or being dragged: the taskbar only matters as a place to
    // dock, but its layout is still what the dock slot comes from.
    if (m_drag || m_model.floating) {
        m_layout = current;
        if (!m_drag) {
            syncFloating();
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
            m_barWidthDip = m_widgetLayout.width;
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
    m_widgetDpi = current->dpi;
    repaintWidget();
}

float Application::floatingHeightDip() const {
    if (!m_layout) {
        return 40.0f;
    }
    const int heightPx =
        win32::height(m_layout->bounds) - 2 * win32::scaleDip(config::widgetVerticalMarginDip, m_layout->dpi);
    return pixelsToDip(heightPx, m_layout->dpi);
}

// Lays the floating widget out, as a bar the size it has in the taskbar or
// as a card of the size the user gave it, at the DPI of its monitor. Returns
// its size in pixels.
SIZE Application::layoutFloating() {
    UINT dpi = m_widget.isFloating() ? GetDpiForWindow(m_widget.hwnd()) : 0;
    if (dpi == 0) {
        dpi = m_layout ? m_layout->dpi : 96;
    }
    m_widgetDpi = dpi;
    const bool card = m_settings.card;
    const float heightDip = floatingHeightDip();
    float widthDip = config::widgetMaxWidthDip / 2.0f;
    if (m_renderer) {
        Result<render::WidgetLayout> widgetLayout =
            card ? m_renderer->layoutCard(m_model) : m_renderer->layout(m_model, heightDip);
        if (widgetLayout) {
            m_widgetLayout = widgetLayout.value();
            widthDip = m_widgetLayout.width;
            if (!card) {
                m_barWidthDip = widthDip;
            }
        } else {
            log::error("{}", widgetLayout.error().describe());
        }
    }
    return SIZE{.cx = dipToPixels(widthDip, dpi), .cy = dipToPixels(card ? m_widgetLayout.height : heightDip, dpi)};
}

// Where it was (or where the settings say), sized for its current content.
void Application::syncFloating() {
    const SIZE size = layoutFloating();
    RECT current{.left = m_settings.floatingX, .top = m_settings.floatingY};
    if (m_widget.isFloating()) {
        GetWindowRect(m_widget.hwnd(), &current);
    }
    placeFloating(keepOnScreen({current.left, current.top, current.left + size.cx, current.top + size.cy}));
}

void Application::placeFloating(const RECT& rect) {
    if (!m_widget.isFloating()) {
        if (const Result<void> created = m_widget.makeFloating(rect); !created) {
            log::error("{}", created.error().describe());
            return;
        }
        log::info("widget floating at ({}, {}) {}x{} px, dpi {}", rect.left, rect.top, win32::width(rect),
                  win32::height(rect), m_widgetDpi);
    } else if (!win32::sameRect(rect, m_widgetRect)) {
        m_widget.move(rect);
    }
    m_widgetRect = rect;
    repaintWidget();
}

// Pressed on an edge of the floating widget: the drag timer follows the
// cursor and resizes from that edge, the opposite edges staying put.
void Application::beginResize(UINT edges) {
    if (m_drag || !m_widget.isFloating()) {
        return;
    }
    if (m_volumeFlyout) {
        m_volumeFlyout->close();
    }
    m_drag = Drag{.edges = edges, .startCard = m_settings.card};
    GetWindowRect(m_widget.hwnd(), &m_drag->startRect);
    GetCursorPos(&m_drag->startCursor);
    m_wheelHook.reset();
    m_peekHoverSince = 0;
    updateQueuePeek();
    SetTimer(m_messageWindow.get(), dragTimerId, config::dragFrameMs, nullptr);
}

// Where the widget would sit if dropped on the taskbar, in screen pixels.
std::optional<RECT> Application::dockSlot() const {
    if (!m_layout) {
        return std::nullopt;
    }
    const float widthDip = m_widgetLayout.card ? (m_barWidthDip > 0.0f ? m_barWidthDip : config::widgetMaxWidthDip / 2.0f)
                                               : m_widgetLayout.width;
    RECT slot = taskbar::placeWidget(*m_layout, dipToPixels(widthDip, m_layout->dpi));
    MapWindowPoints(m_layout->taskbar, nullptr, reinterpret_cast<POINT*>(&slot), 2);
    return slot;
}

void Application::beginDrag(POINT grab) {
    if (m_drag || !m_widget.hwnd()) {
        return;
    }
    if (m_volumeFlyout) {
        m_volumeFlyout->close();
    }
    RECT screen{};
    GetWindowRect(m_widget.hwnd(), &screen);
    m_drag = Drag{.grab = grab};
    // The window under the hook and the bubble is about to be replaced.
    m_wheelHook.reset();
    m_peekHoverSince = 0;
    updateQueuePeek();
    m_model.hover.reset();
    m_model.hoverProgress = 0.0f;

    if (!m_model.floating) {
        m_model.floating = true;
        if (const Result<void> created = m_widget.makeFloating(screen); !created) {
            log::error("{}", created.error().describe());
            m_model.floating = false;
            m_drag.reset();
            syncWithTaskbar(true);
            return;
        }
        m_widgetRect = screen;
        log::info("widget undocked");
    }
    if (!m_dockPreview) {
        if (Result<std::unique_ptr<taskbar::DockPreview>> preview = taskbar::DockPreview::create(m_instance); preview) {
            m_dockPreview = std::move(preview.value());
        } else {
            log::error("dock preview unavailable: {}", preview.error().describe());
        }
    }
    SetTimer(m_messageWindow.get(), dragTimerId, config::dragFrameMs, nullptr);
    repaintWidget();
}

void Application::onDragFrame() {
    if (!m_drag || !m_widget.hwnd()) {
        KillTimer(m_messageWindow.get(), dragTimerId);
        m_drag.reset();
        return;
    }
    const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    if ((GetAsyncKeyState(button) & 0x8000) == 0) {
        endDrag();
        return;
    }

    if (m_drag->edges != 0) {
        // Pulled far enough taller the bar snaps to the card, and pulled far
        // enough shorter the card snaps to the bar; nothing in between.
        // The edges opposite the grabbed one stay where they were.
        POINT cursor{};
        GetCursorPos(&cursor);
        const RECT& start = m_drag->startRect;
        RECT wanted = start;
        const LONG dx = cursor.x - m_drag->startCursor.x;
        const LONG dy = cursor.y - m_drag->startCursor.y;
        wanted.left += (m_drag->edges & taskbar::WidgetWindow::EdgeLeft) ? dx : 0;
        wanted.right += (m_drag->edges & taskbar::WidgetWindow::EdgeRight) ? dx : 0;
        wanted.top += (m_drag->edges & taskbar::WidgetWindow::EdgeTop) ? dy : 0;
        wanted.bottom += (m_drag->edges & taskbar::WidgetWindow::EdgeBottom) ? dy : 0;
        const float grown = pixelsToDip(win32::height(wanted) - win32::height(start), m_widgetDpi);
        const bool card = m_drag->startCard ? grown > -config::cardSnapDeltaDip : grown >= config::cardSnapDeltaDip;
        if (card == m_settings.card) {
            return;  // Still the same size; nothing to move.
        }
        m_settings.card = card;
        const SIZE size = layoutFloating();
        const LONG left = (m_drag->edges & taskbar::WidgetWindow::EdgeLeft) ? start.right - size.cx : start.left;
        const LONG top = (m_drag->edges & taskbar::WidgetWindow::EdgeTop) ? start.bottom - size.cy : start.top;
        placeFloating({left, top, left + size.cx, top + size.cy});
        return;
    }

    // Crossing onto a monitor with another scale resizes the widget; keep
    // the same spot of it under the cursor.
    if (const UINT dpi = GetDpiForWindow(m_widget.hwnd()); dpi != 0 && dpi != m_widgetDpi) {
        m_drag->grab = {MulDiv(m_drag->grab.x, static_cast<int>(dpi), static_cast<int>(m_widgetDpi)),
                        MulDiv(m_drag->grab.y, static_cast<int>(dpi), static_cast<int>(m_widgetDpi))};
        syncFloating();
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    RECT rect = m_widgetRect;
    OffsetRect(&rect, cursor.x - m_drag->grab.x - rect.left, cursor.y - m_drag->grab.y - rect.top);
    if (!win32::sameRect(rect, m_widgetRect)) {
        m_widget.move(rect);
        m_widgetRect = rect;
    }

    bool overDock = false;
    const std::optional<RECT> slot = dockSlot();
    if (slot) {
        RECT zone = m_layout->bounds;
        InflateRect(&zone, 0, win32::scaleDip(static_cast<int>(config::dockSnapDip), m_layout->dpi));
        overDock = PtInRect(&zone, cursor) != FALSE;
    }
    if (overDock != m_drag->overDock) {
        m_drag->overDock = overDock;
        if (m_dockPreview) {
            if (overDock) {
                m_dockPreview->show(*slot, m_layout->dpi);
                m_widget.move(m_widgetRect);  // Back above the preview.
            } else {
                m_dockPreview->hide();
            }
        }
        m_widgetAlpha = overDock ? config::dragOverDockAlpha : 255;
        repaintWidget();
    }
}

void Application::endDrag() {
    KillTimer(m_messageWindow.get(), dragTimerId);
    if (m_dockPreview) {
        m_dockPreview->hide();
    }
    const bool dock = m_drag && m_drag->overDock;
    m_drag.reset();
    m_widgetAlpha = 255;

    if (dock) {
        m_model.floating = false;
        m_settings.floating = false;
        m_settings.card = false;  // In the taskbar it is a bar again.
        saveSettings();
        log::info("widget docked");
        syncWithTaskbar(true);  // Embedding replaces the floating window.
        return;
    }
    const RECT rect = keepOnScreen(m_widgetRect);
    if (!win32::sameRect(rect, m_widgetRect)) {
        m_widget.move(rect);
        m_widgetRect = rect;
    }
    m_settings.floating = true;
    m_settings.floatingX = rect.left;
    m_settings.floatingY = rect.top;
    saveSettings();
    log::info("widget floating at ({}, {})", rect.left, rect.top);
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

    if (const Result<void> drawn = m_renderer->draw(m_surface, m_model, m_widgetLayout, m_widgetDpi); !drawn) {
        log::error("{}", drawn.error().describe());
        return;
    }

    if (const Result<void> presented = m_surface.present(m_widget.hwnd(), m_widgetAlpha); !presented) {
        log::error("{}", presented.error().describe());
        return;
    }
    m_widget.show();

    // Frames keep coming only while the renderer has something moving.
    const bool animating = m_renderer->animating(m_model, m_widgetLayout);
    if (animating != m_animating && m_messageWindow) {
        m_animating = animating;
        if (animating) {
            SetTimer(m_messageWindow.get(), animationTimerId, config::animationFrameMs, nullptr);
        } else {
            KillTimer(m_messageWindow.get(), animationTimerId);
        }
    }
}

void Application::onMediaChanged() {
    const media::NowPlaying now = m_media->snapshot();

    const bool textChanged = now.available ? (m_model.title != now.title || m_model.artist != now.artist)
                                           : (m_model.title != strings().placeholderTitle.wide);
    const bool sessionChanged = m_sessionAvailable != now.available;
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

    syncShuffle(now.shuffle);

    if (now.available) {
        m_model.title = now.title;
        m_model.artist = now.artist;
        m_smtcCoverSettled = !now.coverPending;
        if (now.coverPending) {
            // New track, old artwork: show the placeholder until the new one lands.
            setCover({});
        } else if (m_smtcCoverVersion != now.coverVersion) {
            m_smtcCoverVersion = now.coverVersion;
            setCover(now.cover);
        }
    } else {
        m_model.title = strings().placeholderTitle.wide;
        m_model.artist = strings().placeholderArtist.wide;
        m_smtcCoverSettled = false;
        m_smtcCoverVersion = now.coverVersion;
        setCover({});
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
        m_linksRetries = 0;
        m_artworkRequested.clear();
        forgetUpNext();
        if (now.available && m_spotify && m_spotify->connected()) {
            m_spotify->requestNowPlaying();
        }
    }
    applyArtworkFallback();
    if (textChanged) {
        syncWithTaskbar(true);  // Width may change with the text.
    } else {
        repaintWidget();
    }
}

// The model's cover version is the renderer's cache key, so it moves with
// every change of bytes whichever source they come from.
void Application::setCover(std::vector<std::uint8_t> image) {
    if (image.empty() && m_model.coverImage.empty()) {
        return;
    }
    m_model.coverImage = std::move(image);
    ++m_model.coverVersion;
    updateAccentFromCover();
}

// SMTC is the artwork source, but Spotify now and then publishes a track with
// no thumbnail and never fills it in. With the Web API connected, the album
// art it reports stands in. Returns true when the cover changed.
bool Application::applyArtworkFallback() {
    if (!m_spotify || !m_smtcCoverSettled || !m_model.coverImage.empty() || !linksMatchCurrentTrack() ||
        m_links->artworkUrl.empty()) {
        return false;
    }
    if (std::optional<spotify::Artwork> artwork = m_spotify->artwork(m_links->artworkUrl)) {
        log::info("cover from the Web API ({} bytes)", artwork->bytes.size());
        setCover(std::move(artwork->bytes));
        return true;
    }
    if (m_artworkRequested != m_links->artworkUrl) {
        m_artworkRequested = m_links->artworkUrl;
        log::info("SMTC has no cover; fetching album art from the Web API");
        m_spotify->requestArtwork(m_links->artworkUrl);
    }
    return false;
}

// The Web API can lag SMTC by a track after a quick skip; ask again so exact
// links and the artwork fallback catch up.
void Application::refreshStaleLinks() {
    if (!m_spotify || !m_spotify->connected() || !m_sessionAvailable || !m_links || linksMatchCurrentTrack() ||
        m_linksRetries >= config::spotifyLinksRetryLimit) {
        return;
    }
    ++m_linksRetries;
    m_links.reset();
    m_spotify->requestNowPlaying();
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
    const float x = pixelsToDip(position.x, m_widgetDpi);
    const float y = pixelsToDip(position.y, m_widgetDpi);

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
        case interaction::Zone::Shuffle:
            toggleShuffle();
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
            forgetUpNext();  // That track is what plays now; the bubble returns with the new next.
            break;
        case interaction::Zone::Volume:
            toggleVolumeFlyout();
            break;
        case interaction::Zone::Visualizer:
            toggleColorMode();
            break;
    }
}

void Application::toggleShuffle() {
    if (!m_model.shuffle) {
        return;  // Spotify offers no shuffle control right now.
    }
    const bool active = !*m_model.shuffle;
    m_media->setShuffle(active);
    m_model.shuffle = active;
    m_shuffleHoldUntil = GetTickCount64() + config::shuffleConfirmHoldMs;
    repaintWidget();
}

// Takes SMTC's shuffle state, except that right after a click a contrary
// report is most likely Spotify's stale one; it wins only once the hold ends.
void Application::syncShuffle(const std::optional<bool>& reported) {
    if (reported == m_model.shuffle) {
        m_shuffleHoldUntil = 0;
        return;
    }
    if (reported && GetTickCount64() < m_shuffleHoldUntil) {
        return;
    }
    m_shuffleHoldUntil = 0;
    m_model.shuffle = reported;
    repaintWidget();
}

void Application::toggleVolumeFlyout() {
    if (!m_volumeFlyout) {
        Result<std::unique_ptr<overlay::VolumeFlyout>> flyout = overlay::VolumeFlyout::create(
            m_instance, {
                            .onLevel =
                                [this](float level) {
                                    if (m_volume.read().value_or(audio::VolumeState{}).muted) {
                                        m_volume.setMuted(false);
                                    }
                                    m_volume.setLevel(level);
                                    showVolume(audio::VolumeState{.level = level});
                                },
                            .onToggleMute =
                                [this] {
                                    if (const std::optional<audio::VolumeState> state = m_volume.read()) {
                                        m_volume.setMuted(!state->muted);
                                    }
                                    showVolume(m_volume.read());
                                },
                        });
        if (!flyout) {
            log::error("volume flyout unavailable: {}", flyout.error().describe());
            return;
        }
        m_volumeFlyout = std::move(flyout.value());
    }
    if (m_volumeFlyout->visible()) {
        m_volumeFlyout->close();
        return;
    }
    if (m_volumeFlyout->justClosed() || !m_layout) {
        return;  // This click is what closed it.
    }

    m_volume.refresh();
    const std::optional<audio::VolumeState> state = m_volume.read();
    showVolume(state);
    if (!state) {
        log::info("volume: Spotify has no audio session");
        return;
    }
    m_volumeFlyout->open(zoneOnScreen(m_widgetLayout.volume), m_widgetDpi, state->level, state->muted);
}

// A widget zone in screen pixels, full widget height.
RECT Application::zoneOnScreen(const render::RectF& zone) const {
    RECT widget{};
    GetWindowRect(m_widget.hwnd(), &widget);
    return RECT{
        .left = widget.left + dipToPixels(zone.left, m_widgetDpi),
        .top = widget.top,
        .right = widget.left + dipToPixels(zone.right, m_widgetDpi),
        .bottom = widget.bottom,
    };
}

void Application::onWheel(int delta) {
    const std::optional<audio::VolumeState> state = m_volume.read();
    if (!state) {
        return;
    }
    const float level =
        std::clamp(state->level + config::volumeStep * static_cast<float>(delta) / WHEEL_DELTA, 0.0f, 1.0f);
    const bool unmute = state->muted && delta > 0;
    if (unmute) {
        m_volume.setMuted(false);
    }
    m_volume.setLevel(level);
    m_model.volumeOsdSince = GetTickCount64();
    showVolume(audio::VolumeState{.level = level, .muted = state->muted && !unmute});
    repaintWidget();
}

// Shows the bubble once the pointer has rested on "next" for a moment and
// the queue has answered; hides it otherwise.
void Application::updateQueuePeek() {
    const bool resting =
        m_peekHoverSince != 0 && GetTickCount64() - m_peekHoverSince >= config::queuePeekDelayMs;
    if (!resting || !m_upNextKnown || !m_upNext || !m_spotify) {
        if (m_queuePeek && !resting) {
            m_queuePeek->hide();
        }
        return;
    }
    if (!m_queuePeek) {
        Result<std::unique_ptr<overlay::QueuePeek>> peek = overlay::QueuePeek::create(m_instance);
        if (!peek) {
            log::error("queue peek unavailable: {}", peek.error().describe());
            return;
        }
        m_queuePeek = std::move(peek.value());
    }
    overlay::QueuePeek::Content content{
        .label = strings().upNext.wide,
        .title = m_upNext->name,
        .subtitle = m_upNext->artist,
    };
    if (std::optional<spotify::Artwork> artwork = m_spotify->artwork(m_upNext->artworkUrl)) {
        content.cover = std::move(artwork->bytes);
    }
    m_queuePeek->show(zoneOnScreen(m_widgetLayout.next), m_widgetDpi, content);
}

// Takes a queue answer as what plays next. One still out of step with SMTC
// after the retries (a title the two sources spell differently, say) is
// trusted unless it names what is playing.
void Application::acceptQueue(const spotify::QueueResult& queue) {
    m_upNext = queue.next;
    if (m_upNext && equalsIgnoreCase(m_upNext->name, m_model.title)) {
        m_upNext.reset();
    }
    m_upNextKnown = true;
    if (m_upNext) {
        log::info("up next: {} / {}", text::toUtf8(m_upNext->name), text::toUtf8(m_upNext->artist));
        m_spotify->requestArtwork(m_upNext->artworkUrl);
    } else {
        log::info("up next: nothing to show");
    }
}

// The queue moved on (a new track, or "next" clicked): ask again if the
// pointer is still waiting on the button.
void Application::forgetUpNext() {
    m_upNext.reset();
    m_upNextKnown = false;
    m_queueRequest = 0;
    m_queueRetries = 0;
    if (m_queuePeek) {
        m_queuePeek->hide();
    }
    if (m_peekHoverSince != 0 && m_spotify && m_spotify->connected()) {
        m_queueRequest = m_spotify->requestQueue();
    }
}

void Application::refreshVolume() {
    m_volume.refresh();
    showVolume(m_volume.read());
}

void Application::showVolume(const std::optional<audio::VolumeState>& state) {
    const std::optional<float> shown =
        state ? std::optional<float>{state->muted ? 0.0f : state->level} : std::nullopt;
    if (state && m_volumeFlyout) {
        m_volumeFlyout->setState(state->level, state->muted);
    }
    if (shown != m_model.volume) {
        m_model.volume = shown;
        repaintWidget();
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
        m_widgetLayout, pixelsToDip(position.x, m_widgetDpi), pixelsToDip(position.y, m_widgetDpi));
    if (zone != m_model.hover) {
        m_model.hover = zone;
        repaintWidget();
    }
    setHoverFading(true);

    if (!m_wheelHook) {
        // The widget's window is looked up on every event: docking and
        // undocking replace it while the hook may still be installed.
        const HWND messageWindow = m_messageWindow.get();
        m_wheelHook = std::make_unique<interaction::WheelHook>([this, messageWindow](POINT screen, int delta) {
            RECT bounds{};
            if (!GetWindowRect(m_widget.hwnd(), &bounds) || !PtInRect(&bounds, screen)) {
                return false;
            }
            PostMessageW(messageWindow, WM_THRENODY_WHEEL, static_cast<WPARAM>(static_cast<INT_PTR>(delta)), 0);
            return true;
        });
    }

    const bool overNext = zone == render::Zone::Next;
    if (overNext && m_peekHoverSince == 0) {
        m_peekHoverSince = GetTickCount64();
        if (!m_upNextKnown && m_queueRequest == 0 && m_spotify && m_spotify->connected()) {
            m_queueRequest = m_spotify->requestQueue();
        }
        SetTimer(m_messageWindow.get(), peekTimerId, config::queuePeekDelayMs, nullptr);
    } else if (!overNext && m_peekHoverSince != 0) {
        m_peekHoverSince = 0;
        updateQueuePeek();
    }
}

void Application::onPointerLeave() {
    m_model.hover.reset();
    m_wheelHook.reset();
    m_peekHoverSince = 0;
    updateQueuePeek();
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
    m_settings.colorMode = nextColorMode(m_settings.colorMode);
    m_model.colorMode = m_settings.colorMode;
    log::info("colour mode: {}", colorModeName(m_model.colorMode));
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
            const tray::MenuItem trayMenu[] = {
                {.id = menuSettingsId, .text = strings().menuSettings.wide},
                {.id = menuQuitId, .text = strings().menuQuit.wide, .separatorBefore = true},
            };
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
    // Where the widget lives is decided by dragging it; the window's copy of
    // the settings may predate the last drag.
    m_settings.floating = previous.floating;
    m_settings.floatingX = previous.floatingX;
    m_settings.floatingY = previous.floatingY;
    m_settings.card = previous.card;

    if (previous.colorMode != updated.colorMode) {
        m_model.colorMode = updated.colorMode;
        log::info("colour mode: {}", colorModeName(m_model.colorMode));
        repaintWidget();
    }
    if (previous.language != updated.language) {
        applyLanguage();
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

// A language change touches everything that shows text outside the settings
// window: the overlay, the widget placeholder, and (on demand) the tray menu.
void Application::applyLanguage() {
    log::info("language: {}", i18n::languageCode(m_settings.language));
    if (m_lockOverlay) {
        m_lockOverlay->setLanguage(m_settings.language);
    }
    if (!m_sessionAvailable) {
        m_model.title = strings().placeholderTitle.wide;
        m_model.artist = strings().placeholderArtist.wide;
        syncWithTaskbar(true);
    }
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
        if (applyArtworkFallback()) {
            repaintWidget();
        }
        // The queue answered, or the next track's cover arrived.
        if (m_queueRequest != 0) {
            if (const std::optional<spotify::QueueResult> queue = m_spotify->queue();
                queue && queue->request == m_queueRequest) {
                m_queueRequest = 0;
                const bool stale = !equalsIgnoreCase(queue->playingName, m_model.title);
                if (stale && m_queueRetries < config::queueRetryLimit) {
                    ++m_queueRetries;
                    SetTimer(m_messageWindow.get(), queueRetryTimerId, config::queueRetryMs, nullptr);
                } else {
                    acceptQueue(*queue);
                }
            }
        }
        updateQueuePeek();
    }
    publishSpotifyStatus();
}

void Application::publishSpotifyStatus() {
    if (!m_settingsWindow || !m_spotify) {
        return;
    }
    const spotify::Status status = m_spotify->status();
    m_settingsWindow->setSpotifyStatus({.state = status.state, .detail = status.detail});
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
        m_beat.reset();
        m_model.pulse = 0.0f;
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
    m_model.pulse = m_beat.update(m_analyzer.kickDb(), static_cast<float>(config::spectrumFrameMs));

    if (m_model.colorMode != ColorMode::Track) {
        const float step = static_cast<float>(config::spectrumFrameMs) / (1000.0f * config::rainbowCycleSeconds);
        m_model.rainbowPhase = std::fmod(m_model.rainbowPhase + step, 1.0f);
    }
    repaintWidget();

    if (!m_model.playing && m_analyzer.idle()) {
        setSpectrumRunning(false);
    }
}

}  // namespace threnody
