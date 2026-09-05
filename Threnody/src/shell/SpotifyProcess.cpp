#include "shell/SpotifyProcess.h"

#include "util/Win32.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <vector>

namespace threnody::shell {
namespace {

constexpr std::wstring_view spotifyImageName = L"Spotify.exe";

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

struct Candidate {
    DWORD processId{};
    ULONGLONG creationTime{};  // FILETIME as integer; 0 if unknown.
};

std::vector<Candidate> spotifyProcesses() {
    std::vector<Candidate> result;
    win32::unique_handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{.dwSize = sizeof(PROCESSENTRY32W)};
    for (BOOL ok = Process32FirstW(snapshot.get(), &entry); ok; ok = Process32NextW(snapshot.get(), &entry)) {
        if (!equalsIgnoreCase(entry.szExeFile, spotifyImageName)) {
            continue;
        }
        Candidate candidate{.processId = entry.th32ProcessID};
        win32::unique_handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        if (process) {
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process.get(), &creation, &exit, &kernel, &user)) {
                candidate.creationTime = (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
            }
        }
        result.push_back(candidate);
    }
    return result;
}

struct WindowSearch {
    const std::vector<Candidate>* candidates{};
    HWND found{};
    DWORD owner{};
};

BOOL CALLBACK onWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    const bool spotify = std::any_of(search->candidates->begin(), search->candidates->end(),
                                     [processId](const Candidate& c) { return c.processId == processId; });
    if (!spotify || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 || GetWindowTextLengthW(hwnd) == 0) {
        return TRUE;
    }
    search->found = hwnd;
    search->owner = processId;
    return FALSE;
}

}  // namespace

std::optional<SpotifyProcess> findSpotify() {
    const std::vector<Candidate> candidates = spotifyProcesses();
    if (candidates.empty()) {
        return std::nullopt;
    }

    WindowSearch search{.candidates = &candidates};
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&search));
    if (search.found != nullptr) {
        return SpotifyProcess{.processId = search.owner, .mainWindow = search.found};
    }

    const auto oldest = std::min_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.creationTime < b.creationTime;
    });
    return SpotifyProcess{.processId = oldest->processId};
}

}  // namespace threnody::shell
