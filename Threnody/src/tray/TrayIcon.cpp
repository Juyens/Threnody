#include "tray/TrayIcon.h"

#include "tray/PopupMenu.h"
#include "util/Log.h"

#include <algorithm>
#include <vector>

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
    std::vector<MenuEntry> entries;
    for (const MenuItem& item : items) {
        if (item.separatorBefore) {
            entries.emplace_back();
        }
        entries.push_back({.id = item.id, .text = item.text});
    }
    return showPopupMenu(m_data.hWnd, entries, anchor);
}

}  // namespace threnody::tray
