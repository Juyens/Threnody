#pragma once

namespace threnody {

// How the visualiser bars are coloured. Clicking the visualiser cycles
// through them in this order; persisted in the settings file.
//   Track:         every bar in the cover's dominant colour.
//   Rainbow:       the hue circle sweeping across the bars.
//   TrackGradient: the cover's colour, with hue and lightness rippling
//                  around it and travelling across the bars.
enum class ColorMode { Track, Rainbow, TrackGradient };

[[nodiscard]] constexpr ColorMode nextColorMode(ColorMode mode) noexcept {
    switch (mode) {
        case ColorMode::Track: return ColorMode::Rainbow;
        case ColorMode::Rainbow: return ColorMode::TrackGradient;
        case ColorMode::TrackGradient: return ColorMode::Track;
    }
    return ColorMode::Track;
}

[[nodiscard]] constexpr const char* colorModeName(ColorMode mode) noexcept {
    switch (mode) {
        case ColorMode::Track: return "track";
        case ColorMode::Rainbow: return "rainbow";
        case ColorMode::TrackGradient: return "gradient";
    }
    return "track";
}

}  // namespace threnody
