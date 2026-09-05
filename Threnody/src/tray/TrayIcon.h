#pragma once

#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>
#include <shellapi.h>

#include <span>
#include <string_view>

namespace threnody::tray {

struct MenuItem {
    UINT id{};
    const wchar_t* text{};
    bool separatorBefore{false};
};

// Notification-area icon. Events arrive at `owner` as `callbackMessage` with
// NOTIFYICON_VERSION_4 semantics: LOWORD(lParam) is the event, wParam the
// anchor point. Removed on destruction; `readd` restores it after explorer
// rebuilds the taskbar.
class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callbackMessage, HICON icon, std::wstring_view tooltip);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void readd();
    [[nodiscard]] bool added() const noexcept { return m_added; }

    // Shows a popup menu at `anchor` (screen pixels); returns the chosen id
    // or 0 when dismissed.
    [[nodiscard]] UINT showMenu(std::span<const MenuItem> items, POINT anchor) const;

private:
    void add();

    NOTIFYICONDATAW m_data{};
    bool m_added{false};
};

}  // namespace threnody::tray
