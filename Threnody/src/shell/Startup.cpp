#include "shell/Startup.h"

#include "util/Win32.h"

#include <Windows.h>

#include <array>
#include <string>

namespace threnody::shell {
namespace {

constexpr wchar_t runKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t valueName[] = L"Threnody";

std::wstring quotedExecutablePath() {
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return L"\"" + std::wstring{path.data(), length} + L"\"";
}

}  // namespace

bool isStartWithWindowsEnabled() {
    DWORD type = 0;
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, runKeyPath, valueName, RRF_RT_REG_SZ, &type, nullptr, nullptr);
    return status == ERROR_SUCCESS || status == ERROR_MORE_DATA;
}

Result<void> setStartWithWindows(bool enabled) {
    HKEY rawKey = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, runKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &rawKey, nullptr);
    if (status != ERROR_SUCCESS) {
        return Error::fromWin32(static_cast<DWORD>(status), "open HKCU Run key");
    }
    win32::unique_hkey key{rawKey};

    if (enabled) {
        const std::wstring command = quotedExecutablePath();
        status = RegSetValueExW(key.get(), valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        if (status != ERROR_SUCCESS) {
            return Error::fromWin32(static_cast<DWORD>(status), "write Run entry");
        }
    } else {
        status = RegDeleteValueW(key.get(), valueName);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            return Error::fromWin32(static_cast<DWORD>(status), "delete Run entry");
        }
    }
    return {};
}

}  // namespace threnody::shell
