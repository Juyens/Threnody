#pragma once

// Straight (non-premultiplied) RGBA colour, components in [0, 1]. Domain type
// shared by the colour extraction code and the renderer; knows nothing about
// Direct2D.
namespace threnody {

struct Color {
    float r{};
    float g{};
    float b{};
    float a{1.0f};

    [[nodiscard]] constexpr Color withAlpha(float alpha) const noexcept { return {r, g, b, alpha}; }
};

}  // namespace threnody
