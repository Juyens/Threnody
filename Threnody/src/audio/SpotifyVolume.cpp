#include "audio/SpotifyVolume.h"

#include "util/Win32.h"

#include <mmdeviceapi.h>

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <vector>

namespace threnody::audio {
namespace {

constexpr std::wstring_view spotifyImageName = L"Spotify.exe";

bool isSpotifyProcess(DWORD processId) {
    win32::unique_handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process) {
        return false;
    }
    wchar_t path[MAX_PATH]{};
    DWORD length = MAX_PATH;
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &length)) {
        return false;
    }
    std::wstring_view image{path, length};
    if (const auto slash = image.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
        image.remove_prefix(slash + 1);
    }
    return image.size() == spotifyImageName.size() &&
           std::equal(image.begin(), image.end(), spotifyImageName.begin(),
                      [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}

struct Session {
    winrt::com_ptr<ISimpleAudioVolume> volume;
    bool active{false};
};

std::vector<Session> spotifySessions() {
    std::vector<Session> result;
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put())))) {
        return result;
    }
    winrt::com_ptr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put()))) {
        return result;
    }
    UINT deviceCount = 0;
    devices->GetCount(&deviceCount);
    for (UINT d = 0; d < deviceCount; ++d) {
        winrt::com_ptr<IMMDevice> device;
        winrt::com_ptr<IAudioSessionManager2> manager;
        winrt::com_ptr<IAudioSessionEnumerator> sessions;
        if (FAILED(devices->Item(d, device.put())) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void())) ||
            FAILED(manager->GetSessionEnumerator(sessions.put()))) {
            continue;
        }
        int sessionCount = 0;
        sessions->GetCount(&sessionCount);
        for (int s = 0; s < sessionCount; ++s) {
            winrt::com_ptr<IAudioSessionControl> control;
            if (FAILED(sessions->GetSession(s, control.put()))) {
                continue;
            }
            const auto control2 = control.try_as<IAudioSessionControl2>();
            AudioSessionState state{};
            DWORD processId = 0;
            if (!control2 || control2->IsSystemSoundsSession() == S_OK || FAILED(control2->GetState(&state)) ||
                state == AudioSessionStateExpired || FAILED(control2->GetProcessId(&processId)) ||
                !isSpotifyProcess(processId)) {
                continue;
            }
            if (auto volume = control.try_as<ISimpleAudioVolume>()) {
                result.push_back({.volume = std::move(volume), .active = state == AudioSessionStateActive});
            }
        }
    }
    // The playing session speaks for Spotify when there are several.
    std::stable_partition(result.begin(), result.end(), [](const Session& s) { return s.active; });
    return result;
}

}  // namespace

void SpotifyVolume::refresh() {
    m_sessions.clear();
    for (Session& session : spotifySessions()) {
        m_sessions.push_back(std::move(session.volume));
    }
}

template <typename Apply>
bool SpotifyVolume::forEachSession(Apply apply) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1 || m_sessions.empty()) {
            refresh();
        }
        bool any = false;
        for (const auto& volume : m_sessions) {
            any = SUCCEEDED(apply(*volume)) || any;
        }
        if (any) {
            return true;
        }
    }
    return false;
}

std::optional<VolumeState> SpotifyVolume::read() {
    VolumeState state;
    bool done = false;
    const bool ok = forEachSession([&](ISimpleAudioVolume& volume) {
        if (done) {
            return S_OK;  // The first (playing) session speaks for Spotify.
        }
        BOOL muted = FALSE;
        HRESULT hr = volume.GetMasterVolume(&state.level);
        if (SUCCEEDED(hr)) {
            hr = volume.GetMute(&muted);
        }
        state.muted = muted != FALSE;
        done = SUCCEEDED(hr);
        return hr;
    });
    if (!ok) {
        return std::nullopt;
    }
    return state;
}

bool SpotifyVolume::setLevel(float level) {
    level = std::clamp(level, 0.0f, 1.0f);
    return forEachSession([level](ISimpleAudioVolume& volume) { return volume.SetMasterVolume(level, nullptr); });
}

bool SpotifyVolume::setMuted(bool muted) {
    return forEachSession([muted](ISimpleAudioVolume& volume) { return volume.SetMute(muted ? TRUE : FALSE, nullptr); });
}

}  // namespace threnody::audio
