#pragma once

#include "color/Color.h"

#include <cstdint>
#include <span>

namespace threnody::color {

// Picks the colour that best represents a cover for use on a dark taskbar:
// the most common saturated hue, averaged, then pushed to a saturation and
// lightness that stay legible. Pixels are 32-bit BGRA. Covers that are
// essentially grey or black return `fallback`.
[[nodiscard]] Color dominantColor(std::span<const std::uint32_t> bgraPixels, const Color& fallback) noexcept;

}  // namespace threnody::color
