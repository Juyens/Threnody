#include "render/WidgetLayout.h"

#include "Config.h"

#include <algorithm>

namespace threnody::render {

WidgetLayout WidgetLayout::compute(float height, float titleWidth, float titleHeight, float artistWidth,
                                   float artistHeight) noexcept {
    using namespace config;

    const float coverSize = height - 2.0f * widgetPaddingDip;
    const float controlsWidth = 5.0f * controlButtonWidthDip;
    const float visualizerWidth =
        spectrumBarCount * spectrumBarWidthDip + (spectrumBarCount - 1) * spectrumBarGapDip;

    float textWidth = std::clamp(std::max(titleWidth, artistWidth), 0.0f, textMaxWidthDip);
    const float fixedWidth = 2.0f * widgetPaddingDip + coverSize + 3.0f * widgetGapDip + controlsWidth + visualizerWidth;
    textWidth = std::min(textWidth, static_cast<float>(widgetMaxWidthDip) - fixedWidth);
    textWidth = std::max(textWidth, 0.0f);

    WidgetLayout layout{};
    layout.height = height;
    layout.width = fixedWidth + textWidth;
    layout.cornerRadius = backgroundCornerRadiusDip;
    layout.coverCornerRadius = coverCornerRadiusDip;

    float x = widgetPaddingDip;
    layout.cover = {x, widgetPaddingDip, x + coverSize, widgetPaddingDip + coverSize};
    x += coverSize + widgetGapDip;

    const float textBlockHeight = titleHeight + textLineGapDip + artistHeight;
    const float textTop = (height - textBlockHeight) / 2.0f;
    layout.title = {x, textTop, x + textWidth, textTop + titleHeight};
    layout.artist = {x, layout.title.bottom + textLineGapDip, x + textWidth, layout.title.bottom + textLineGapDip + artistHeight};
    x += textWidth + widgetGapDip;

    layout.shuffle = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.previous = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.playPause = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.next = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip;
    layout.repeat = {x, 0.0f, x + controlButtonWidthDip, height};
    x += controlButtonWidthDip + widgetGapDip;

    layout.visualizer = {x, widgetPaddingDip, x + visualizerWidth, height - widgetPaddingDip};
    return layout;
}

WidgetLayout WidgetLayout::computeCard(float titleHeight, float artistHeight) noexcept {
    using namespace config;
    WidgetLayout layout{};
    layout.card = true;
    layout.controlScale = cardControlScale;
    layout.width = cardWidthDip;
    layout.cornerRadius = cardCornerRadiusDip;
    layout.coverCornerRadius = cardCoverCornerRadiusDip;

    const float inner = cardWidthDip - 2.0f * cardPaddingDip;
    layout.cover = {cardPaddingDip, cardPaddingDip, cardPaddingDip + inner, cardPaddingDip + inner};
    float y = layout.cover.bottom + cardGapDip;
    layout.title = {cardPaddingDip, y, cardPaddingDip + inner, y + titleHeight};
    layout.artist = {cardPaddingDip, layout.title.bottom + textLineGapDip, cardPaddingDip + inner,
                     layout.title.bottom + textLineGapDip + artistHeight};
    y = layout.artist.bottom + cardGapDip;
    const float button = std::min(inner / 5.0f, cardControlMaxWidthDip);
    float x = (cardWidthDip - 5.0f * button) / 2.0f;
    for (RectF* zone : {&layout.shuffle, &layout.previous, &layout.playPause, &layout.next, &layout.repeat}) {
        *zone = {x, y, x + button, y + cardControlsHeightDip};
        x += button;
    }
    layout.height = y + cardControlsHeightDip + cardPaddingDip;
    return layout;
}

}  // namespace threnody::render
