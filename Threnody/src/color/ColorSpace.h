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

// OKLCH: the polar form of OKLab. Equal steps in L look equally bright and
// equal steps in h look equally far apart, which HSL/HSV cannot promise, so
// the visualiser palettes are built here. Lightness in [0, 1], chroma about
// [0, 0.37], hue in degrees.
struct Oklch {
    float l{};
    float c{};
    float h{};
};

[[nodiscard]] Oklch toOklch(const Color& color) noexcept;

// Colours outside sRGB are brought back by reducing chroma, not by clipping
// channels, so the hue and lightness asked for are kept.
[[nodiscard]] Color fromOklch(const Oklch& lch, float alpha = 1.0f) noexcept;

}  // namespace threnody::color
