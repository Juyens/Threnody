#pragma once

#include "render/Fonts.h"
#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "render/WidgetLayout.h"
#include "render/WidgetModel.h"
#include "util/Result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace threnody::render {

// Draws the widget with Direct2D into a LayeredSurface through a DC render
// target. Device resources are created once and reused; text layouts and the
// decoded cover are cached until their inputs change.
class WidgetRenderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<WidgetRenderer>> create();

    // Measures the model's text and lays the widget out for `heightDip`.
    [[nodiscard]] Result<WidgetLayout> layout(const WidgetModel& model, float heightDip);
    // The same for the card, whose size is fixed.
    [[nodiscard]] Result<WidgetLayout> layoutCard(const WidgetModel& model);

    // Renders one frame into `surface` (already sized in pixels) at `dpi`.
    [[nodiscard]] Result<void> draw(LayeredSurface& surface, const WidgetModel& model, const WidgetLayout& layout,
                                    UINT dpi);

    // True while something on the widget moves by itself (a cover turning, a
    // title sliding, text scrolling), so the owner keeps drawing frames.
    [[nodiscard]] bool animating(const WidgetModel& model, const WidgetLayout& layout) const noexcept;

    // Shared factories, for callers that decode images or draw icons.
    [[nodiscard]] IWICImagingFactory& wic() const noexcept { return *m_graphics.wic; }
    [[nodiscard]] Graphics& graphics() noexcept { return m_graphics; }

private:
    struct TextLine {
        std::wstring text;
        float maxWidth{};
        IDWriteTextFormat* format{};
        winrt::com_ptr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
    };

    // Decoded cover, scaled so its shorter side matches the cover square in
    // pixels; `source` is the centred square to draw from.
    struct Cover {
        std::uint32_t version{};
        int sizePx{};
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        D2D1_RECT_F source{};
    };

    // The floating widget's backdrop: the cover blurred to the widget's size
    // in DIPs, as a brush so it can fill the rounded panel.
    struct Backdrop {
        std::uint32_t version{};
        int widthDip{};
        int heightDip{};
        winrt::com_ptr<ID2D1BitmapBrush> brush;
    };

    WidgetRenderer(Graphics graphics, Fonts fonts);

    [[nodiscard]] Result<void> ensureTarget(const LayeredSurface& surface, UINT dpi);
    // An icon's paths, stroked together (see render/Icons.h).
    using Icon = std::vector<winrt::com_ptr<ID2D1PathGeometry>>;

    [[nodiscard]] Result<void> ensureIcons();
    // Strokes `icon` in a `size`-DIP box at `origin`, with the current brush.
    void drawIcon(const Icon& icon, D2D1_POINT_2F origin, float size);
    [[nodiscard]] Result<winrt::com_ptr<IWICBitmapFrameDecode>> decodeCover(const WidgetModel& model);
    [[nodiscard]] Result<void> ensureCover(const WidgetModel& model, const RectF& zone);
    [[nodiscard]] Result<void> ensureBackdrop(const WidgetModel& model, const WidgetLayout& layout);
    [[nodiscard]] Result<void> updateTextLine(TextLine& line, const std::wstring& text, float maxWidth,
                                              IDWriteTextFormat& format);
    void releaseDeviceResources() noexcept;
    void noteTextChange(const WidgetModel& model);
    [[nodiscard]] Result<void> measureText(const WidgetModel& model, IDWriteTextFormat& title, IDWriteTextFormat& artist);

    void drawBackground(const WidgetLayout& layout, const WidgetModel& model);
    void drawHoverHighlight(const WidgetLayout& layout, const WidgetModel& model);
    void drawBackdrop(const WidgetLayout& layout, const WidgetModel& model, const D2D1_ROUNDED_RECT& shape);
    void drawPulse(const WidgetLayout& layout, const WidgetModel& model);
    void drawSeparator(const WidgetLayout& layout);
    void drawProgress(const WidgetLayout& layout, const WidgetModel& model);
    void drawSeekTime(const WidgetLayout& layout, const WidgetModel& model);
    void drawCover(const WidgetLayout& layout, const WidgetModel& model);
    void drawCoverFace(const std::optional<Cover>& face, const D2D1_ROUNDED_RECT& shape);
    void drawCardVolume(const WidgetLayout& layout, const WidgetModel& model);
    void drawText(const WidgetLayout& layout);
    void drawTextLine(const TextLine& line, const winrt::com_ptr<IDWriteTextLayout>& natural, const RectF& box,
                      const Color& color, float opacity, float lift);
    void drawControls(const WidgetLayout& layout, const WidgetModel& model);
    void drawSpectrum(const WidgetLayout& layout, const WidgetModel& model);

    void fill(const Color& color);
    [[nodiscard]] static Color barColor(const WidgetModel& model, int bar);
    [[nodiscard]] static float volumeOsdOpacity(const WidgetModel& model) noexcept;
    [[nodiscard]] float marqueeOffset(const TextLine& line, const winrt::com_ptr<IDWriteTextLayout>& natural,
                                      float boxWidth) const noexcept;

    Graphics m_graphics;
    Fonts m_fonts;

    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    HDC m_boundDc{};
    SIZE m_boundSize{};
    UINT m_dpi{96};

    winrt::com_ptr<ID2D1StrokeStyle> m_iconStroke;  // Round caps and joins, as Lucide draws.
    Icon m_shuffleIcon;
    Icon m_sparkleIcon;
    Icon m_repeatIcon;
    Icon m_repeatOneIcon;
    Icon m_previousIcon;
    Icon m_playIcon;
    Icon m_pauseIcon;
    Icon m_nextIcon;
    std::array<Icon, 4> m_speakerIcons;  // Muted, low, mid, high.

    TextLine m_title;
    TextLine m_artist;
    // The same text laid out without a width limit: what scrolls.
    winrt::com_ptr<IDWriteTextLayout> m_titleNatural;
    winrt::com_ptr<IDWriteTextLayout> m_artistNatural;
    std::optional<Cover> m_cover;

    // Transitions, timed with GetTickCount64. Zero start means none running.
    std::optional<Cover> m_coverFrom;  // What the flip started from; empty = placeholder.
    ULONGLONG m_coverFlipStart{};
    TextLine m_titleFrom;
    TextLine m_artistFrom;
    ULONGLONG m_textSlideStart{};
    ULONGLONG m_hoverSince{};  // Marquee clock: when the pointer arrived.

    std::optional<Backdrop> m_backdrop;
    std::optional<Backdrop> m_backdropFrom;
    ULONGLONG m_backdropFadeStart{};
};

}  // namespace threnody::render
