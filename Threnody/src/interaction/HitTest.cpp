#include "interaction/HitTest.h"

namespace threnody::interaction {

Zone hitTest(const render::WidgetLayout& layout, float x, float y) noexcept {
    if (layout.cover.contains(x, y)) {
        return Zone::Cover;
    }
    if (layout.title.contains(x, y)) {
        return Zone::Title;
    }
    if (layout.artist.contains(x, y)) {
        return Zone::Artist;
    }
    if (layout.shuffle.contains(x, y)) {
        return Zone::Shuffle;
    }
    if (layout.previous.contains(x, y)) {
        return Zone::Previous;
    }
    if (layout.playPause.contains(x, y)) {
        return Zone::PlayPause;
    }
    if (layout.next.contains(x, y)) {
        return Zone::Next;
    }
    if (layout.repeat.contains(x, y)) {
        return Zone::Repeat;
    }
    // The visualiser zone is generous: the whole column, not just the bars.
    // The card has none.
    if (!layout.card && x >= layout.visualizer.left && x < layout.visualizer.right && y >= 0.0f &&
        y < layout.height) {
        return Zone::Visualizer;
    }
    return Zone::Background;
}

}  // namespace threnody::interaction
