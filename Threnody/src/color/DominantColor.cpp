#include "color/DominantColor.h"

#include "color/ColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace threnody::color {
namespace {

constexpr int hueBins = 36;
constexpr float minSaturation = 0.18f;
constexpr float minLightness = 0.10f;
constexpr float maxLightness = 0.92f;
constexpr float minColourfulShare = 0.04f;  // Below this the cover is treated as grey.

// Where the result is allowed to land so it reads on the taskbar.
constexpr float outputMinSaturation = 0.55f;
constexpr float outputMinLightness = 0.55f;
constexpr float outputMaxLightness = 0.72f;

struct Bin {
    float weight{};
    float r{};
    float g{};
    float b{};
};

Color unpack(std::uint32_t bgra) noexcept {
    return {static_cast<float>((bgra >> 16) & 0xFF) / 255.0f, static_cast<float>((bgra >> 8) & 0xFF) / 255.0f,
            static_cast<float>(bgra & 0xFF) / 255.0f, static_cast<float>((bgra >> 24) & 0xFF) / 255.0f};
}

}  // namespace

Color dominantColor(std::span<const std::uint32_t> bgraPixels, const Color& fallback) noexcept {
    if (bgraPixels.empty()) {
        return fallback;
    }

    std::array<Bin, hueBins> bins{};
    std::size_t colourful = 0;
    for (const std::uint32_t packed : bgraPixels) {
        const Color c = unpack(packed);
        if (c.a < 0.5f) {
            continue;
        }
        const Hsl hsl = toHsl(c);
        if (hsl.s < minSaturation || hsl.l < minLightness || hsl.l > maxLightness) {
            continue;
        }
        ++colourful;
        // Saturated, mid-lightness pixels count more: they are the ones a
        // person would call "the colour of the cover".
        const float weight = hsl.s * (1.0f - std::abs(hsl.l - 0.5f) * 1.6f);
        if (weight <= 0.0f) {
            continue;
        }
        Bin& bin = bins[static_cast<std::size_t>(std::min(hueBins - 1, static_cast<int>(hsl.h * hueBins)))];
        bin.weight += weight;
        bin.r += c.r * weight;
        bin.g += c.g * weight;
        bin.b += c.b * weight;
    }

    if (static_cast<float>(colourful) < minColourfulShare * static_cast<float>(bgraPixels.size())) {
        return fallback;
    }

    // Best bin including its neighbours, so a hue straddling a boundary wins.
    int best = 0;
    float bestWeight = -1.0f;
    for (int i = 0; i < hueBins; ++i) {
        const float weight = bins[static_cast<std::size_t>(i)].weight +
                             0.5f * (bins[static_cast<std::size_t>((i + 1) % hueBins)].weight +
                                     bins[static_cast<std::size_t>((i + hueBins - 1) % hueBins)].weight);
        if (weight > bestWeight) {
            bestWeight = weight;
            best = i;
        }
    }

    Bin merged{};
    for (const int offset : {-1, 0, 1}) {
        const Bin& bin = bins[static_cast<std::size_t>((best + offset + hueBins) % hueBins)];
        merged.weight += bin.weight;
        merged.r += bin.r;
        merged.g += bin.g;
        merged.b += bin.b;
    }
    if (merged.weight <= 0.0f) {
        return fallback;
    }

    Hsl hsl = toHsl({merged.r / merged.weight, merged.g / merged.weight, merged.b / merged.weight, 1.0f});
    hsl.s = std::max(hsl.s, outputMinSaturation);
    hsl.l = std::clamp(hsl.l, outputMinLightness, outputMaxLightness);
    return fromHsl(hsl);
}

}  // namespace threnody::color
