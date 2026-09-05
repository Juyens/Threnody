#include "color/ColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

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

// sRGB transfer function and the OKLab matrices (Bjorn Ottosson, 2020).
float linearFromSrgb(float c) noexcept {
    return c > 0.04045f ? std::pow((c + 0.055f) / 1.055f, 2.4f) : c / 12.92f;
}

float srgbFromLinear(float c) noexcept {
    return c > 0.0031308f ? 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f : 12.92f * c;
}

struct Oklab {
    float l{};
    float a{};
    float b{};
};

Oklab toOklab(const Color& color) noexcept {
    const float r = linearFromSrgb(color.r);
    const float g = linearFromSrgb(color.g);
    const float b = linearFromSrgb(color.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

// Linear sRGB, unclamped, so the caller can tell whether the colour fits.
std::array<float, 3> linearFromOklab(const Oklab& lab) noexcept {
    const float l_ = lab.l + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    const float m_ = lab.l - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
    const float s_ = lab.l - 0.0894841775f * lab.a - 1.2914855480f * lab.b;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

bool inGamut(const std::array<float, 3>& linear) noexcept {
    constexpr float slack = 1e-4f;
    return std::ranges::all_of(linear, [](float c) { return c >= -slack && c <= 1.0f + slack; });
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

Oklch toOklch(const Color& color) noexcept {
    const Oklab lab = toOklab(color);
    const float chroma = std::hypot(lab.a, lab.b);
    if (chroma < 1e-5f) {
        return {lab.l, 0.0f, 0.0f};
    }
    float hue = std::atan2(lab.b, lab.a) * 180.0f / std::numbers::pi_v<float>;
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    return {lab.l, chroma, hue};
}

Color fromOklch(const Oklch& lch, float alpha) noexcept {
    const float radians = lch.h * std::numbers::pi_v<float> / 180.0f;
    const float cosH = std::cos(radians);
    const float sinH = std::sin(radians);
    const float l = std::clamp(lch.l, 0.0f, 1.0f);

    // Bisect the chroma down until the colour is displayable: a dozen steps
    // are plenty for a per-bar colour, and the result keeps its hue.
    float chroma = std::max(lch.c, 0.0f);
    std::array<float, 3> linear = linearFromOklab({l, chroma * cosH, chroma * sinH});
    if (!inGamut(linear)) {
        float low = 0.0f;
        float high = chroma;
        for (int i = 0; i < 12; ++i) {
            const float middle = (low + high) / 2.0f;
            if (inGamut(linearFromOklab({l, middle * cosH, middle * sinH}))) {
                low = middle;
            } else {
                high = middle;
            }
        }
        linear = linearFromOklab({l, low * cosH, low * sinH});
    }
    const auto channel = [](float value) { return std::clamp(srgbFromLinear(std::clamp(value, 0.0f, 1.0f)), 0.0f, 1.0f); };
    return {channel(linear[0]), channel(linear[1]), channel(linear[2]), alpha};
}

}  // namespace threnody::color
