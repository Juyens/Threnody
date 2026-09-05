#include "taskbar/Taskbar.h"

#include "Config.h"
#include "util/Win32.h"

namespace threnody::taskbar {

bool Layout::sameGeometry(const Layout& other) const noexcept {
    return taskbar == other.taskbar && win32::sameRect(bounds, other.bounds) && dpi == other.dpi &&
           alignment == other.alignment && tray.has_value() == other.tray.has_value() &&
           (!tray || win32::sameRect(*tray, *other.tray));
}

Alignment readAlignment() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, explorerAdvancedKey, L"TaskbarAl", RRF_RT_REG_DWORD,
                                        nullptr, &value, &size);
    if (status != ERROR_SUCCESS) {
        return Alignment::Center;
    }
    return value == 0 ? Alignment::Left : Alignment::Center;
}

std::optional<Layout> queryLayout() {
    const HWND taskbar = FindWindowW(taskbarClassName, nullptr);
    if (taskbar == nullptr) {
        return std::nullopt;
    }

    // A freshly created Shell_TrayWnd reports a degenerate rectangle for a
    // moment; treat it as absent and let the next tick pick it up.
    RECT bounds{};
    if (!GetWindowRect(taskbar, &bounds) || win32::width(bounds) < win32::height(bounds) ||
        win32::height(bounds) <= 0) {
        return std::nullopt;
    }

    Layout layout{
        .taskbar = taskbar,
        .bounds = bounds,
        .dpi = GetDpiForWindow(taskbar),
        .alignment = readAlignment(),
    };
    if (layout.dpi == 0) {
        layout.dpi = GetDpiForSystem();
    }

    const HWND tray = FindWindowExW(taskbar, nullptr, trayClassName, nullptr);
    RECT trayRect{};
    if (tray != nullptr && GetWindowRect(tray, &trayRect) && win32::width(trayRect) > 0) {
        layout.tray = trayRect;
    }
    return layout;
}

RECT placeWidget(const Layout& layout, int widgetWidthPx) {
    const int verticalMargin = win32::scaleDip(config::widgetVerticalMarginDip, layout.dpi);
    const int edgeMargin = win32::scaleDip(config::widgetEdgeMarginDip, layout.dpi);
    const int height = win32::height(layout.bounds) - 2 * verticalMargin;

    // Right limit in screen coordinates: the tray's left edge when the icons
    // are left-aligned, otherwise unused.
    POINT anchor{.x = layout.bounds.left, .y = layout.bounds.top};
    if (layout.alignment == Alignment::Left) {
        anchor.x = layout.tray ? layout.tray->left : layout.bounds.right;
    }
    MapWindowPoints(HWND_DESKTOP, layout.taskbar, &anchor, 1);

    const int left = layout.alignment == Alignment::Left ? anchor.x - edgeMargin - widgetWidthPx
                                                         : anchor.x + edgeMargin;
    const int top = anchor.y + verticalMargin;
    return RECT{.left = left, .top = top, .right = left + widgetWidthPx, .bottom = top + height};
}

}  // namespace threnody::taskbar
