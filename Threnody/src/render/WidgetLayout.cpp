#include "render/WidgetLayout.h"

#include "Config.h"

#include <algorithm>

namespace threnody::render {

WidgetLayout WidgetLayout::compute(float height, float titleWidth, float titleHeight, float artistWidth,
                                   float artistHeight) noexcept {
    using namespace config;

    const float coverSize = height - 2.0f * widgetPaddingDip;
    const float controlsWidth = 3.0f * controlButtonWidthDip;
    const float visualizerWidth =
        spectrumBarCount * spectrumBarWidthDip + (spectrumBarCount - 1) * spectrumBarGapDip;

    float textWidth = std::clamp(std::max(titleWidth, artistWidth), 0.0f, textMaxWidthDip);
    const float fixedWidth = 2.0f * widgetPaddingDip + coverSize + 3.0f * widgetGapDip + controlsWidth + visualizerWidth;
    textWidth = std::min(textWidth, static_cast<float>(widgetMaxWidthDip) - fixedWidth);
    textWidth = std::max(textWidth, 0.0f);

    WidgetLayout layout{};
    layout.height = height;
    layout.width = fixedWidth + textWidth;

    float x = widgetPaddingDip;
    layout.cover = {x, widgetPaddingDip, x + coverSize, widgetPaddingDip + coverSize};
    x += coverSize + widgetGapDip;

    const float textBlockHeight = titleHeight + textLineGapDip + artistHeight;
    const float textTop = (height - textBlockHeight) / 2.0f;
    layout.title = {x, textTop, x + textWidth, textTop + titleHeight};
    layout.artist = {x, layout.title.bottom + textLineGapDip, x + textWidth, layout.title.bottom + textLineGapDip + artistHeight};
    x += textWidth + widgetGapDip;

    layout.previous = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.playPause = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.next = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip + widgetGapDip;

    layout.visualizer = {x, widgetPaddingDip, x + visualizerWidth, height - widgetPaddingDip};
    return layout;
}

}  // namespace threnody::render
