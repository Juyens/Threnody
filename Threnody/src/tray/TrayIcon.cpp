#include "tray/TrayIcon.h"

#include "util/Log.h"

#include <algorithm>

namespace threnody::tray {

TrayIcon::TrayIcon(HWND owner, UINT callbackMessage, HICON icon, std::wstring_view tooltip) {
    m_data = NOTIFYICONDATAW{
        .cbSize = sizeof(NOTIFYICONDATAW),
        .hWnd = owner,
        .uID = 1,
        .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP,
        .uCallbackMessage = callbackMessage,
        .hIcon = icon,
    };
    const std::size_t length = std::min(tooltip.size(), std::size(m_data.szTip) - 1);
    std::copy_n(tooltip.data(), length, m_data.szTip);
    m_data.szTip[length] = L'\0';
    m_data.uVersion = NOTIFYICON_VERSION_4;
    add();
}

TrayIcon::~TrayIcon() {
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_data);
    }
}

void TrayIcon::add() {
    if (!Shell_NotifyIconW(NIM_ADD, &m_data)) {
        log::warn("{}", Error::fromLastError("Shell_NotifyIcon(NIM_ADD)").describe());
        m_added = false;
        return;
    }
    Shell_NotifyIconW(NIM_SETVERSION, &m_data);
    m_added = true;
}

void TrayIcon::readd() {
    // After a taskbar rebuild the old registration is gone; NIM_DELETE on it
    // is harmless and keeps the shell's bookkeeping straight.
    Shell_NotifyIconW(NIM_DELETE, &m_data);
    add();
}

UINT TrayIcon::showMenu(std::span<const MenuItem> items, POINT anchor) const {
    win32::unique_hmenu menu{CreatePopupMenu()};
    if (!menu) {
        return 0;
    }
    for (const MenuItem& item : items) {
        if (item.separatorBefore) {
            AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(menu.get(), MF_STRING, item.id, item.text);
    }

    // The owner must be foreground for the menu to close when the user
    // clicks elsewhere; the WM_NULL afterwards is the documented nudge.
    SetForegroundWindow(m_data.hWnd);
    const UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                                           anchor.x, anchor.y, m_data.hWnd, nullptr));
    PostMessageW(m_data.hWnd, WM_NULL, 0, 0);
    return chosen;
}

}  // namespace threnody::tray
