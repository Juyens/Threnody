#pragma once

#include "util/Win32.h"

#include <Windows.h>

#include <thread>

namespace threnody::taskbar {

// Posts `message` to `target` every time a value under the watched key
// changes. Waits on a dedicated thread; RegNotifyChangeKeyValue has no
// callback form.
class RegistryWatcher {
public:
    RegistryWatcher(HKEY root, const wchar_t* subKey, HWND target, UINT message);
    ~RegistryWatcher();

    RegistryWatcher(const RegistryWatcher&) = delete;
    RegistryWatcher& operator=(const RegistryWatcher&) = delete;

    [[nodiscard]] bool running() const noexcept { return m_thread.joinable(); }

private:
    void watch(std::stop_token stop);

    win32::unique_hkey m_key;
    win32::unique_handle m_changed;
    win32::unique_handle m_stopped;
    HWND m_target{};
    UINT m_message{};
    std::jthread m_thread;
};

}  // namespace threnody::taskbar
