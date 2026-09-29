#include "settings/Settings.h"

#include "util/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace threnody::settings {
namespace {

using nlohmann::json;

ColorMode colorModeFromName(const std::string& name) noexcept {
    if (name == "rainbow") {
        return ColorMode::Rainbow;
    }
    if (name == "gradient") {
        return ColorMode::TrackGradient;
    }
    return ColorMode::Track;
}

json toJson(const Settings& s) {
    return json{
        {"setupShown", s.setupShown},
        {"language", std::string{i18n::languageCode(s.language)}},
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
        {"beatPulse", s.beatPulse},
        {"vinylCard", s.vinylCard},
        {"visualizerStyle", visualizerStyleName(s.visualizerStyle)},
        {"vinylRing", s.vinylRing},
        {"wavyProgress", s.wavyProgress},
        {"widget",
         {
             {"floating", s.floating},
             {"x", s.floatingX},
             {"y", s.floatingY},
             {"card", s.card},
         }},
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
    s.language = i18n::languageFromCode(j.value("language", std::string{i18n::languageCode(s.language)}));
    s.startWithWindows = j.value("startWithWindows", s.startWithWindows);
    if (const auto keys = j.find("lockKeys"); keys != j.end() && keys->is_object()) {
        s.lockKeys.enabled = keys->value("enabled", s.lockKeys.enabled);
        s.lockKeys.capsLock = keys->value("capsLock", s.lockKeys.capsLock);
        s.lockKeys.numLock = keys->value("numLock", s.lockKeys.numLock);
        s.lockKeys.scrollLock = keys->value("scrollLock", s.lockKeys.scrollLock);
        s.lockKeys.insert = keys->value("insert", s.lockKeys.insert);
    }
    s.colorMode = colorModeFromName(j.value("colorMode", std::string{colorModeName(s.colorMode)}));
    s.beatPulse = j.value("beatPulse", s.beatPulse);
    s.vinylCard = j.value("vinylCard", s.vinylCard);
    s.visualizerStyle = visualizerStyleFromName(
        j.value("visualizerStyle", std::string{visualizerStyleName(s.visualizerStyle)}));
    s.vinylRing = j.value("vinylRing", s.vinylRing);
    s.wavyProgress = j.value("wavyProgress", s.wavyProgress);
    if (const auto widget = j.find("widget"); widget != j.end() && widget->is_object()) {
        s.floating = widget->value("floating", s.floating);
        s.floatingX = widget->value("x", s.floatingX);
        s.floatingY = widget->value("y", s.floatingY);
        s.card = widget->value("card", s.card);
    }
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
