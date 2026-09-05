#include "app/Application.h"

#include "Config.h"
#include "util/Log.h"

namespace threnody {
namespace {

constexpr wchar_t messageClassName[] = L"ThrenodyMessageWindow";

constexpr UINT_PTR healthTimerId = 1;
constexpr UINT WM_THRENODY_ALIGNMENT_CHANGED = WM_APP + 1;

constexpr const char* alignmentName(taskbar::Alignment alignment) noexcept {
    return alignment == taskbar::Alignment::Left ? "left" : "center";
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

    const int widthPx = win32::scaleDip(config::widgetWidthDip, current->dpi);
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
    const SIZE size{.cx = win32::width(m_widgetRect), .cy = win32::height(m_widgetRect)};
    if (const Result<void> resized = m_surface.resize(size); !resized) {
        log::error("{}", resized.error().describe());
        return;
    }

    // Placeholder until Direct2D takes over: a solid, deliberately visible
    // fill. Premultiplied BGRA, alpha 255.
    constexpr std::uint32_t placeholder = 0xFFE01B6A;
    for (std::uint32_t& pixel : m_surface.pixels()) {
        pixel = placeholder;
    }

    if (const Result<void> presented = m_surface.present(m_widget.hwnd()); !presented) {
        log::error("{}", presented.error().describe());
        return;
    }
    m_widget.show();
}

}  // namespace threnody
