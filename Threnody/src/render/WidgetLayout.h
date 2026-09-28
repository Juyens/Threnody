#pragma once

namespace threnody::render {

// The clickable parts of the widget. Shared by hit-testing and by the
// renderer, which highlights the one under the pointer.
enum class Zone { Background, Cover, Title, Artist, Shuffle, Previous, PlayPause, Next, Repeat, Visualizer, Progress };

struct RectF {
    float left{};
    float top{};
    float right{};
    float bottom{};

    [[nodiscard]] constexpr float width() const noexcept { return right - left; }
    [[nodiscard]] constexpr float height() const noexcept { return bottom - top; }
    [[nodiscard]] constexpr bool contains(float x, float y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

// Zones of the widget in device-independent pixels, relative to its top-left
// corner. Computed from the widget height and the measured text widths, so it
// is plain arithmetic: the renderer uses it to draw and the interaction code
// uses it to hit-test, without either knowing about the other.
struct WidgetLayout {
    float width{};
    float height{};
    bool card{false};           // The floating widget stretched into a vertical card.
    float controlScale{1.0f};   // Glyph and icon size relative to the bar's.
    float cornerRadius{};       // Of the panel.
    float coverCornerRadius{};
    RectF cover;
    RectF title;
    RectF artist;
    RectF shuffle;
    RectF previous;
    RectF playPause;
    RectF next;
    RectF repeat;
    RectF visualizer;
    RectF progress;  // The strip along the bottom edge that seeks.

    // `titleWidth`/`artistWidth` are the natural widths of the text; they are
    // clamped to the configured maximum, and the overall width to the widget
    // maximum, with the text column absorbing the difference.
    [[nodiscard]] static WidgetLayout compute(float height, float titleWidth, float titleHeight, float artistWidth,
                                              float artistHeight) noexcept;

    // The card, always cardWidthDip wide: the cover across the top, then
    // title and artist, then the controls spread across. No visualiser (its
    // zone is empty); the height follows from the text.
    [[nodiscard]] static WidgetLayout computeCard(float titleHeight, float artistHeight) noexcept;
};

}  // namespace threnody::render
