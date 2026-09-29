#pragma once

#include "color/ColorMode.h"
#include "i18n/Strings.h"
#include "render/VisualizerStyle.h"
#include "util/Result.h"

#include <filesystem>
#include <string>

namespace threnody::settings {

struct LockKeyOverlay {
    bool enabled{true};
    bool capsLock{true};
    bool numLock{true};
    bool scrollLock{true};
    bool insert{true};

    bool operator==(const LockKeyOverlay&) const = default;
};

// The few preferences that are not compile-time constants. Stored as JSON in
// the data directory; unknown or malformed fields fall back to defaults.
struct Settings {
    bool setupShown{false};  // The settings window opens itself once, on first run.
    i18n::Language language{i18n::Language::Spanish};
    bool startWithWindows{false};
    LockKeyOverlay lockKeys;
    ColorMode colorMode{ColorMode::Track};
    bool beatPulse{true};  // The border glows on kicks.
    bool vinylCard{true};  // The card's cover spins like a record.
    VisualizerStyle visualizerStyle{VisualizerStyle::Bars};
    bool vinylRing{true};     // The spectrum as rays around the card's record.
    bool wavyProgress{true};  // The played part of the progress line ripples.

    // Dragged out of the taskbar: floats with its top-left corner here, in
    // screen pixels.
    bool floating{false};
    int floatingX{};
    int floatingY{};
    bool card{false};  // Floating as the player card rather than the bar.

    // Spotify Web API (PKCE). Empty until the user connects in the settings
    // window; the refresh token is stored encrypted with DPAPI, base64 here.
    std::string spotifyClientId;
    std::string spotifyRefreshTokenProtected;

    bool operator==(const Settings&) const = default;
};

inline constexpr wchar_t fileName[] = L"settings.json";

// Missing file yields defaults; a malformed file yields defaults and a log
// line, never a failure.
[[nodiscard]] Settings load(const std::filesystem::path& file);
[[nodiscard]] Result<void> save(const Settings& settings, const std::filesystem::path& file);

}  // namespace threnody::settings
