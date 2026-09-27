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
    if (layout.volume.contains(x, y)) {
        return Zone::Volume;
    }
    // In the bar the visualiser zone is generous: the whole column, not just
    // the bars. The card has other things above it.
    const float top = layout.card ? layout.visualizer.top : 0.0f;
    const float bottom = layout.card ? layout.visualizer.bottom : layout.height;
    if (x >= layout.visualizer.left && x < layout.visualizer.right && y >= top && y < bottom) {
        return Zone::Visualizer;
    }
    return Zone::Background;
}

}  // namespace threnody::interaction
