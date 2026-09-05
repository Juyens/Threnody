#pragma once

#include "render/Fonts.h"
#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "render/WidgetLayout.h"
#include "render/WidgetModel.h"
#include "util/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace threnody::render {

// Draws the widget with Direct2D into a LayeredSurface through a DC render
// target. Device resources are created once and reused; text layouts and the
// decoded cover are cached until their inputs change.
class WidgetRenderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<WidgetRenderer>> create();

    // Measures the model's text and lays the widget out for `heightDip`.
    [[nodiscard]] Result<WidgetLayout> layout(const WidgetModel& model, float heightDip);

    // Renders one frame into `surface` (already sized in pixels) at `dpi`.
    [[nodiscard]] Result<void> draw(LayeredSurface& surface, const WidgetModel& model, const WidgetLayout& layout,
                                    UINT dpi);

    // Shared imaging factory, for callers that decode images for analysis.
    [[nodiscard]] IWICImagingFactory& wic() const noexcept { return *m_graphics.wic; }

private:
    struct TextLine {
        std::wstring text;
        float maxWidth{};
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

    WidgetRenderer(Graphics graphics, Fonts fonts);

    [[nodiscard]] Result<void> ensureTarget(const LayeredSurface& surface, UINT dpi);
    [[nodiscard]] Result<void> ensureGlyphs();
    [[nodiscard]] Result<void> ensureCover(const WidgetModel& model, const RectF& zone);
    [[nodiscard]] Result<void> updateTextLine(TextLine& line, const std::wstring& text, float maxWidth,
                                              IDWriteTextFormat& format);
    void releaseDeviceResources() noexcept;

    void drawBackground(const WidgetLayout& layout);
    void drawCover(const WidgetLayout& layout, const WidgetModel& model);
    void drawText(const WidgetLayout& layout);
    void drawControls(const WidgetLayout& layout, const WidgetModel& model);
    void drawSpectrum(const WidgetLayout& layout, const WidgetModel& model);

    void fill(const Color& color);

    Graphics m_graphics;
    Fonts m_fonts;

    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    HDC m_boundDc{};
    SIZE m_boundSize{};
    UINT m_dpi{96};

    winrt::com_ptr<ID2D1PathGeometry> m_playGlyph;
    winrt::com_ptr<ID2D1PathGeometry> m_previousGlyph;
    winrt::com_ptr<ID2D1PathGeometry> m_nextGlyph;

    TextLine m_title;
    TextLine m_artist;
    std::optional<Cover> m_cover;
};

}  // namespace threnody::render
