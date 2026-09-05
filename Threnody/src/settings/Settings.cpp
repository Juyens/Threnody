#include "settings/Settings.h"

#include "util/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace threnody::settings {
namespace {

using nlohmann::json;

constexpr const char* colorModeName(ColorMode mode) noexcept {
    return mode == ColorMode::Rainbow ? "rainbow" : "track";
}

ColorMode colorModeFromName(const std::string& name) noexcept {
    return name == "rainbow" ? ColorMode::Rainbow : ColorMode::Track;
}

json toJson(const Settings& s) {
    return json{
        {"setupShown", s.setupShown},
        {"startWithWindows", s.startWithWindows},
        {"lockKeys",
         {
             {"enabled", s.lockKeys.enabled},
             {"capsLock", s.lockKeys.capsLock},
             {"numLock", s.lockKeys.numLock},
             {"scrollLock", s.lockKeys.scrollLock},
             {"insert", s.lockKeys.insert},
         }},
        {"colorMode", colorModeName(s.colorMode)},
        {"spotify",
         {
             {"clientId", s.spotifyClientId},
             {"refreshTokenProtected", s.spotifyRefreshTokenProtected},
         }},
    };
}

Settings fromJson(const json& j) {
    Settings s;
    s.setupShown = j.value("setupShown", s.setupShown);
    s.startWithWindows = j.value("startWithWindows", s.startWithWindows);
    if (const auto keys = j.find("lockKeys"); keys != j.end() && keys->is_object()) {
        s.lockKeys.enabled = keys->value("enabled", s.lockKeys.enabled);
        s.lockKeys.capsLock = keys->value("capsLock", s.lockKeys.capsLock);
        s.lockKeys.numLock = keys->value("numLock", s.lockKeys.numLock);
        s.lockKeys.scrollLock = keys->value("scrollLock", s.lockKeys.scrollLock);
        s.lockKeys.insert = keys->value("insert", s.lockKeys.insert);
    }
    s.colorMode = colorModeFromName(j.value("colorMode", std::string{colorModeName(s.colorMode)}));
    if (const auto spotify = j.find("spotify"); spotify != j.end() && spotify->is_object()) {
        s.spotifyClientId = spotify->value("clientId", s.spotifyClientId);
        s.spotifyRefreshTokenProtected = spotify->value("refreshTokenProtected", s.spotifyRefreshTokenProtected);
    }
    return s;
}

}  // namespace

Settings load(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return Settings{};
    }
    try {
        const json j = json::parse(in, nullptr, true, true);
        if (!j.is_object()) {
            log::warn("settings file is not a JSON object; using defaults");
            return Settings{};
        }
        return fromJson(j);
    } catch (const json::exception& e) {
        log::warn("settings file unreadable ({}); using defaults", e.what());
        return Settings{};
    }
}

Result<void> save(const Settings& settings, const std::filesystem::path& file) {
    // Write next to the target and rename, so a crash mid-write cannot leave
    // a truncated settings file behind.
    auto temporary = file;
    temporary += L".tmp";
    {
        std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
        if (!out) {
            return Error::fromLastError("open settings file for writing");
        }
        out << toJson(settings).dump(2) << '\n';
        if (!out) {
            return Error::fromLastError("write settings file");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        return Error::fromWin32(static_cast<DWORD>(ec.value()), "replace settings file");
    }
    return {};
}

}  // namespace threnody::settings
