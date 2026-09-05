#pragma once

#include "Config.h"
#include "color/Color.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace threnody::render {

// Everything the renderer needs to draw one frame. Filled by the media and
// audio code, read by the renderer; no Win32 or Direct2D types.
struct WidgetModel {
    std::wstring title;
    std::wstring artist;
    bool playing{false};

    // Encoded image (PNG/JPEG bytes) or empty for the placeholder. The
    // version changes whenever the bytes do, so the renderer can cache the
    // decoded bitmap without comparing buffers.
    std::vector<std::uint8_t> coverImage;
    std::uint32_t coverVersion{};

    // Bar heights in [0, 1], bass to treble.
    std::array<float, config::spectrumBarCount> spectrum{};
    Color accent{config::defaultAccentColor};
};

}  // namespace threnody::render
