#pragma once

#include "render/WidgetLayout.h"

namespace threnody::interaction {

enum class Zone { Background, Cover, Title, Artist, Previous, PlayPause, Next, Visualizer };

// Maps a point in widget device-independent pixels to the zone under it.
[[nodiscard]] Zone hitTest(const render::WidgetLayout& layout, float x, float y) noexcept;

}  // namespace threnody::interaction
