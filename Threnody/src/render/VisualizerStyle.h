#pragma once

#include <string_view>

namespace threnody {

// How the visualiser draws the music; persisted by name.
//   Bars:   columns rising from the bottom.
//   Mirror: columns growing both ways from the middle.
//   Curve:  a smooth line through the bands over a softly filled area.
//   Wave:   the audio's waveform itself, like an oscilloscope.
//   Led:    columns of lit segments with slowly falling peaks, like an old
//           stereo.
//   None:   no visualiser; the widget narrows and the beat pulse remains.
enum class VisualizerStyle { Bars, Mirror, Curve, Wave, Led, None };

inline constexpr VisualizerStyle visualizerStyles[] = {VisualizerStyle::Bars, VisualizerStyle::Mirror,
                                                       VisualizerStyle::Curve, VisualizerStyle::Wave,
                                                       VisualizerStyle::Led, VisualizerStyle::None};

[[nodiscard]] constexpr const char* visualizerStyleName(VisualizerStyle style) noexcept {
    switch (style) {
        case VisualizerStyle::Bars: return "bars";
        case VisualizerStyle::Mirror: return "mirror";
        case VisualizerStyle::Curve: return "curve";
        case VisualizerStyle::Wave: return "wave";
        case VisualizerStyle::Led: return "led";
        case VisualizerStyle::None: return "none";
    }
    return "bars";
}

// Unknown names fall back to bars.
[[nodiscard]] constexpr VisualizerStyle visualizerStyleFromName(std::string_view name) noexcept {
    for (const VisualizerStyle style : visualizerStyles) {
        if (name == visualizerStyleName(style)) {
            return style;
        }
    }
    return VisualizerStyle::Bars;
}

}  // namespace threnody
