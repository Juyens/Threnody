#pragma once

#include <Windows.h>

#include <optional>

// Read-only view of the primary taskbar: where it is, where its tray starts,
// and how the user has aligned its icons. Nothing here mutates the shell.
namespace threnody::taskbar {

enum class Alignment { Left, Center };

struct Layout {
    HWND taskbar{};             // Shell_TrayWnd
    RECT bounds{};              // Screen pixels.
    std::optional<RECT> tray;   // TrayNotifyWnd, screen pixels; absent while the shell rebuilds.
    UINT dpi{96};
    Alignment alignment{Alignment::Center};

    [[nodiscard]] bool sameGeometry(const Layout& other) const noexcept;
};

inline constexpr wchar_t taskbarClassName[] = L"Shell_TrayWnd";
inline constexpr wchar_t secondaryTaskbarClassName[] = L"Shell_SecondaryTrayWnd";
inline constexpr wchar_t trayClassName[] = L"TrayNotifyWnd";

// HKCU key holding TaskbarAl. Watched for live alignment changes.
inline constexpr wchar_t explorerAdvancedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

// TaskbarAl: 0 = left, 1 = centered. Missing value means the Windows 11 default, centered.
[[nodiscard]] Alignment readAlignment();

// Empty while Shell_TrayWnd does not exist (explorer restarting).
[[nodiscard]] std::optional<Layout> queryLayout();

// Where the widget goes, in the taskbar's client coordinates. Left-aligned
// icons leave the right side free, so the widget sits just before the tray;
// centered icons leave the left edge free.
[[nodiscard]] RECT placeWidget(const Layout& layout, int widgetWidthPx);

}  // namespace threnody::taskbar
