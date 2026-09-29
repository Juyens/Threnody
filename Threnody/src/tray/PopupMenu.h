#pragma once

#include <Windows.h>

#include <span>
#include <vector>

namespace threnody::tray {

// One line of a popup menu. Without text it is a separator; with children
// it opens a submenu. A radio entry shows a dot instead of a tick.
struct MenuEntry {
    UINT id{};
    const wchar_t* text{};
    bool checked{false};
    bool radio{false};
    bool enabled{true};
    std::vector<MenuEntry> children;
};

// Shows the menu at `anchor` (screen pixels) for `owner`, dark or light as
// Windows' own menus are; returns the chosen id, or 0 when dismissed.
// `above` opens it upward from the anchor, as menus from the taskbar do.
[[nodiscard]] UINT showPopupMenu(HWND owner, std::span<const MenuEntry> entries, POINT anchor, bool above = false);

}  // namespace threnody::tray
