#pragma once

#include "Config.h"
#include "color/Color.h"
#include "color/ColorMode.h"

#include "render/WidgetLayout.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace threnody::render {

// Everything the renderer needs to draw one frame. Filled by the media and
// audio code, read by the renderer; no Win32 or Direct2D types.
struct WidgetModel {
    std::wstring title;
    std::wstring artist;
    bool playing{false};
    bool floating{false};  // Out of the taskbar: drawn on its own opaque panel.

    // Shuffle as Spotify reports it; empty while it offers no control, which
    // draws the button dimmed.
    std::optional<bool> shuffle;
    // Spotify's volume in the Windows mixer, 0 when muted; empty when it has
    // no audio session.
    std::optional<float> volume;
    // When the wheel last changed the volume (GetTickCount64), 0 if never:
    // the visualiser shows the level for a moment instead of the bars.
    std::uint64_t volumeOsdSince{};

    // Encoded image (PNG/JPEG bytes) or empty for the placeholder. The
    // version changes whenever the bytes do, so the renderer can cache the
    // decoded bitmap without comparing buffers.
    std::vector<std::uint8_t> coverImage;
    std::uint32_t coverVersion{};

    // Bar heights in [0, 1], bass to treble.
    std::array<float, config::spectrumBarCount> spectrum{};

    // Track mode paints every bar with `accent` (extracted from the cover);
    // rainbow mode sweeps the hue across the bars, offset by `rainbowPhase`
    // in [0, 1) which advances every frame.
    ColorMode colorMode{ColorMode::Track};
    Color accent{config::defaultAccentColor};
    float rainbowPhase{};

    // Beat glow in [0, 1]: jumps to 1 on a kick in the bass, then fades.
    float pulse{};

    // Pointer feedback: the zone under the pointer, and how far the whole
    // widget has faded toward its hovered look (0 = idle, 1 = hovered).
    std::optional<Zone> hover;
    float hoverProgress{};
};

}  // namespace threnody::render
