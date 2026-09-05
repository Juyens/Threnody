#include "app/Application.h"

#include "Config.h"
#include "util/Log.h"

#include <cmath>

namespace threnody {
namespace {

constexpr wchar_t messageClassName[] = L"ThrenodyMessageWindow";

constexpr UINT_PTR healthTimerId = 1;
constexpr UINT WM_THRENODY_ALIGNMENT_CHANGED = WM_APP + 1;

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
      m_widget(instance) {
    // A real (not message-only) top-level window, otherwise it would miss the
    // TaskbarCreated broadcast. Never shown.
    m_messageWindow.reset(CreateWindowExW(WS_EX_TOOLWINDOW, m_messageClass.name(), L"Threnody", WS_OVERLAPPED,
                                          0, 0, 0, 0, nullptr, nullptr, instance, this));
    if (!m_messageWindow) {
        log::error("{}", Error::fromLastError("CreateWindowEx(ThrenodyMessageWindow)").describe());
        return;
    }

    if (Result<std::unique_ptr<render::WidgetRenderer>> renderer = render::WidgetRenderer::create(); renderer) {
        m_renderer = std::move(renderer.value());
    } else {
        log::error("renderer unavailable: {}", renderer.error().describe());
    }

    // Sample content until the media session feeds the model.
    m_model.title = L"あぶく";
    m_model.artist = L"ヨルシカ";

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    m_alignmentWatcher = std::make_unique<taskbar::RegistryWatcher>(
        HKEY_CURRENT_USER, taskbar::explorerAdvancedKey, m_messageWindow.get(), WM_THRENODY_ALIGNMENT_CHANGED);

    SetTimer(m_messageWindow.get(), healthTimerId, config::taskbarHealthCheckMs, nullptr);
    syncWithTaskbar();
}

Application::~Application() {
    // The watcher posts to the message window; stop it before the window goes.
    m_alignmentWatcher.reset();
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
        syncWithTaskbar();
        return 0;
    }

    switch (message) {
        case WM_THRENODY_ALIGNMENT_CHANGED:
        case WM_TIMER:
            syncWithTaskbar();
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, healthTimerId);
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
void Application::syncWithTaskbar() {
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
    if (embedded && !layoutChanged) {
        return;
    }

    if (!m_layout || m_layout->alignment != current->alignment) {
        log::info("taskbar alignment: {}", alignmentName(current->alignment));
    }

    // Height follows the taskbar; width follows the content.
    const int heightPx = win32::height(current->bounds) - 2 * win32::scaleDip(config::widgetVerticalMarginDip, current->dpi);
    int widthPx = win32::scaleDip(config::widgetMaxWidthDip, current->dpi) / 2;
    if (m_renderer) {
        if (Result<render::WidgetLayout> widgetLayout = m_renderer->layout(m_model, pixelsToDip(heightPx, current->dpi));
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
        log::info("widget moved to ({}, {}) {}x{} px", rect.left, rect.top, win32::width(rect),
                  win32::height(rect));
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

}  // namespace threnody
