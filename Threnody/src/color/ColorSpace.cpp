#include "color/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace threnody::color {
namespace {

float wrap01(float value) noexcept {
    return value - std::floor(value);
}

float hueToChannel(float p, float q, float t) noexcept {
    t = wrap01(t);
    if (t < 1.0f / 6.0f) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < 2.0f / 3.0f) {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    }
    return p;
}

}  // namespace

Hsl toHsl(const Color& color) noexcept {
    const float maxC = std::max({color.r, color.g, color.b});
    const float minC = std::min({color.r, color.g, color.b});
    const float l = (maxC + minC) / 2.0f;
    const float delta = maxC - minC;
    if (delta < 1e-6f) {
        return {0.0f, 0.0f, l};
    }

    const float s = l > 0.5f ? delta / (2.0f - maxC - minC) : delta / (maxC + minC);
    float h;
    if (maxC == color.r) {
        h = (color.g - color.b) / delta + (color.g < color.b ? 6.0f : 0.0f);
    } else if (maxC == color.g) {
        h = (color.b - color.r) / delta + 2.0f;
    } else {
        h = (color.r - color.g) / delta + 4.0f;
    }
    return {h / 6.0f, s, l};
}

Color fromHsl(const Hsl& hsl, float alpha) noexcept {
    if (hsl.s <= 0.0f) {
        return {hsl.l, hsl.l, hsl.l, alpha};
    }
    const float q = hsl.l < 0.5f ? hsl.l * (1.0f + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
    const float p = 2.0f * hsl.l - q;
    return {hueToChannel(p, q, hsl.h + 1.0f / 3.0f), hueToChannel(p, q, hsl.h), hueToChannel(p, q, hsl.h - 1.0f / 3.0f),
            alpha};
}

Color fromHsv(float h, float s, float v, float alpha) noexcept {
    h = wrap01(h) * 6.0f;
    const int sector = static_cast<int>(h);
    const float f = h - static_cast<float>(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (sector % 6) {
        case 0: return {v, t, p, alpha};
        case 1: return {q, v, p, alpha};
        case 2: return {p, v, t, alpha};
        case 3: return {p, q, v, alpha};
        case 4: return {t, p, v, alpha};
        default: return {v, p, q, alpha};
    }
}

}  // namespace threnody::color
