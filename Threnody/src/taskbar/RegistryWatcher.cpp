#include "taskbar/RegistryWatcher.h"

#include "util/Log.h"
#include "util/Result.h"
#include "util/Text.h"

namespace threnody::taskbar {

RegistryWatcher::RegistryWatcher(HKEY root, const wchar_t* subKey, HWND target, UINT message)
    : m_target(target), m_message(message) {
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(root, subKey, 0, KEY_NOTIFY, &key);
    if (status != ERROR_SUCCESS) {
        log::warn("{}", Error::fromWin32(static_cast<DWORD>(status),
                                         std::format("RegOpenKeyEx({})", text::toUtf8(subKey))).describe());
        return;
    }
    m_key.reset(key);

    m_changed.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    m_stopped.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!m_changed || !m_stopped) {
        log::warn("{}", Error::fromLastError("CreateEvent for registry watcher").describe());
        return;
    }

    m_thread = std::jthread{[this](std::stop_token stop) { watch(stop); }};
}

RegistryWatcher::~RegistryWatcher() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        SetEvent(m_stopped.get());
        m_thread.join();
    }
}

void RegistryWatcher::watch(std::stop_token stop) {
    while (!stop.stop_requested()) {
        const LSTATUS status = RegNotifyChangeKeyValue(m_key.get(), FALSE,
                                                       REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC,
                                                       m_changed.get(), TRUE);
        if (status != ERROR_SUCCESS) {
            log::warn("{}", Error::fromWin32(static_cast<DWORD>(status), "RegNotifyChangeKeyValue").describe());
            return;
        }

        const HANDLE handles[] = {m_stopped.get(), m_changed.get()};
        const DWORD signalled = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (signalled == WAIT_OBJECT_0 + 1) {
            PostMessageW(m_target, m_message, 0, 0);
        } else {
            return;
        }
    }
}

}  // namespace threnody::taskbar
