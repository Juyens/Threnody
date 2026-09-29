#include "tray/PopupMenu.h"

#include "util/Win32.h"

namespace threnody::tray {
namespace {

HMENU build(std::span<const MenuEntry> entries) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return nullptr;
    }
    UINT position = 0;
    for (const MenuEntry& entry : entries) {
        MENUITEMINFOW item{.cbSize = sizeof(MENUITEMINFOW)};
        if (!entry.text) {
            item.fMask = MIIM_FTYPE;
            item.fType = MFT_SEPARATOR;
        } else {
            item.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE;
            item.fType = entry.radio ? MFT_RADIOCHECK : MFT_STRING;
            item.fState = (entry.checked ? MFS_CHECKED : 0u) | (entry.enabled ? 0u : MFS_DISABLED);
            item.wID = entry.id;
            item.dwTypeData = const_cast<wchar_t*>(entry.text);  // Only read.
            if (!entry.children.empty()) {
                item.fMask |= MIIM_SUBMENU;
                item.hSubMenu = build(entry.children);  // Owned by the parent from here on.
            }
        }
        InsertMenuItemW(menu, position++, TRUE, &item);
    }
    return menu;
}

// Windows mode (the taskbar's and Start's theme), not the apps' one: these
// menus open from the taskbar.
bool systemUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value != 0;
}

// Win32 menus are light unless the process opts into dark ones through
// uxtheme's SetPreferredAppMode (ordinal 135) and FlushMenuThemes (136).
// Both are undocumented but stable since Windows 10 1903, and what Explorer
// and Notepad++ use; if they are missing the menus simply stay light.
void followWindowsTheme() {
    enum class AppMode : int { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
    using SetPreferredAppMode = AppMode(WINAPI*)(AppMode);
    using FlushMenuThemes = void(WINAPI*)();
    static const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) {
        return;
    }
    static const auto setMode =
        reinterpret_cast<SetPreferredAppMode>(reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))));
    static const auto flush =
        reinterpret_cast<FlushMenuThemes>(reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))));
    if (!setMode) {
        return;
    }
    setMode(systemUsesLightTheme() ? AppMode::ForceLight : AppMode::ForceDark);
    if (flush) {
        flush();
    }
}

}  // namespace

UINT showPopupMenu(HWND owner, std::span<const MenuEntry> entries, POINT anchor, bool above) {
    followWindowsTheme();  // Each time: the theme may have changed since the last menu.
    const win32::unique_hmenu menu{build(entries)};
    if (!menu) {
        return 0;
    }
    // The owner must be foreground for the menu to close when the user
    // clicks elsewhere; the WM_NULL afterwards is the documented nudge.
    SetForegroundWindow(owner);
    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | (above ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
    const auto chosen = static_cast<UINT>(TrackPopupMenuEx(menu.get(), flags, anchor.x, anchor.y, owner, nullptr));
    PostMessageW(owner, WM_NULL, 0, 0);
    return chosen;
}

}  // namespace threnody::tray
