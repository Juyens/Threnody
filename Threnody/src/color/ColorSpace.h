#pragma once

#include "color/Color.h"

namespace threnody::color {

struct Hsl {
    float h{};  // [0, 1)
    float s{};  // [0, 1]
    float l{};  // [0, 1]
};

[[nodiscard]] Hsl toHsl(const Color& color) noexcept;
[[nodiscard]] Color fromHsl(const Hsl& hsl, float alpha = 1.0f) noexcept;

// Hue in [0, 1), saturation and value in [0, 1].
[[nodiscard]] Color fromHsv(float h, float s, float v, float alpha = 1.0f) noexcept;

}  // namespace threnody::color
