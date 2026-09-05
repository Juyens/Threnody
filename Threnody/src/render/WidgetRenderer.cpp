#include "render/WidgetRenderer.h"

#include "Config.h"

#include <algorithm>
#include <cmath>

namespace threnody::render {
namespace {

constexpr D2D1_COLOR_F toD2D(const Color& color) noexcept {
    return D2D1_COLOR_F{color.r, color.g, color.b, color.a};
}

constexpr D2D1_RECT_F toD2D(const RectF& rect) noexcept {
    return D2D1_RECT_F{rect.left, rect.top, rect.right, rect.bottom};
}

// A very wide layout box: measures the natural width of a line.
constexpr float measureWidth = 4096.0f;

Result<winrt::com_ptr<ID2D1PathGeometry>> createPolygon(ID2D1Factory1& factory, std::span<const D2D1_POINT_2F> points,
                                                       const char* what) {
    winrt::com_ptr<ID2D1PathGeometry> geometry;
    HRESULT hr = factory.CreatePathGeometry(geometry.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, std::string("CreatePathGeometry ") + what);
    }
    winrt::com_ptr<ID2D1GeometrySink> sink;
    hr = geometry->Open(sink.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, std::string("ID2D1PathGeometry::Open ") + what);
    }
    sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    hr = sink->Close();
    if (FAILED(hr)) {
        return Error::fromHResult(hr, std::string("ID2D1GeometrySink::Close ") + what);
    }
    return geometry;
}

}  // namespace

WidgetRenderer::WidgetRenderer(Graphics graphics, Fonts fonts)
    : m_graphics(std::move(graphics)), m_fonts(std::move(fonts)) {}

Result<std::unique_ptr<WidgetRenderer>> WidgetRenderer::create() {
    Result<Graphics> graphics = Graphics::create();
    if (!graphics) {
        return graphics.error();
    }
    Result<Fonts> fonts = Fonts::create(*graphics->dwrite);
    if (!fonts) {
        return fonts.error();
    }
    std::unique_ptr<WidgetRenderer> renderer{new WidgetRenderer(std::move(graphics.value()), std::move(fonts.value()))};
    if (const Result<void> glyphs = renderer->ensureGlyphs(); !glyphs) {
        return glyphs.error();
    }
    return renderer;
}

Result<void> WidgetRenderer::ensureGlyphs() {
    constexpr float s = config::controlGlyphSizeDip;
    constexpr float bar = 0.22f * s;

    const D2D1_POINT_2F play[] = {{0.0f, 0.0f}, {s, s / 2.0f}, {0.0f, s}};
    // Previous: a bar on the left, a triangle pointing at it.
    const D2D1_POINT_2F previous[] = {{0.0f, 0.0f}, {bar, 0.0f},         {bar, s / 2.0f}, {s, 0.0f},
                                      {s, s},       {bar, s / 2.0f},     {bar, s},        {0.0f, s}};
    const D2D1_POINT_2F next[] = {{0.0f, 0.0f}, {s - bar, s / 2.0f}, {s - bar, 0.0f}, {s, 0.0f},
                                  {s, s},       {s - bar, s},        {s - bar, s / 2.0f}, {0.0f, s}};

    Result<winrt::com_ptr<ID2D1PathGeometry>> geometry = createPolygon(*m_graphics.d2d, play, "play");
    if (!geometry) {
        return geometry.error();
    }
    m_playGlyph = std::move(geometry.value());

    geometry = createPolygon(*m_graphics.d2d, previous, "previous");
    if (!geometry) {
        return geometry.error();
    }
    m_previousGlyph = std::move(geometry.value());

    geometry = createPolygon(*m_graphics.d2d, next, "next");
    if (!geometry) {
        return geometry.error();
    }
    m_nextGlyph = std::move(geometry.value());
    return {};
}

Result<void> WidgetRenderer::updateTextLine(TextLine& line, const std::wstring& text, float maxWidth,
                                            IDWriteTextFormat& format) {
    if (line.layout && line.text == text && line.maxWidth == maxWidth) {
        return {};
    }
    winrt::com_ptr<IDWriteTextLayout> layout;
    const HRESULT hr = m_graphics.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), &format,
                                                           maxWidth, 1024.0f, layout.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateTextLayout");
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);

    line.text = text;
    line.maxWidth = maxWidth;
    line.layout = std::move(layout);
    line.metrics = metrics;
    return {};
}

Result<WidgetLayout> WidgetRenderer::layout(const WidgetModel& model, float heightDip) {
    // Measure at unlimited width first, lay out, then rebuild the layouts at
    // the width they actually get so trimming applies.
    if (const Result<void> r = updateTextLine(m_title, model.title, measureWidth, m_fonts.title()); !r) {
        return r.error();
    }
    if (const Result<void> r = updateTextLine(m_artist, model.artist, measureWidth, m_fonts.artist()); !r) {
        return r.error();
    }

    const WidgetLayout result = WidgetLayout::compute(
        heightDip, std::ceil(m_title.metrics.widthIncludingTrailingWhitespace), m_title.metrics.height,
        std::ceil(m_artist.metrics.widthIncludingTrailingWhitespace), m_artist.metrics.height);

    if (const Result<void> r = updateTextLine(m_title, model.title, result.title.width(), m_fonts.title()); !r) {
        return r.error();
    }
    if (const Result<void> r = updateTextLine(m_artist, model.artist, result.artist.width(), m_fonts.artist()); !r) {
        return r.error();
    }
    return result;
}

void WidgetRenderer::releaseDeviceResources() noexcept {
    m_cover.reset();  // Bitmaps belong to the target.
    m_brush = nullptr;
    m_target = nullptr;
    m_boundDc = nullptr;
    m_boundSize = {};
}

// Decodes the model's cover with WIC, scales it so the shorter side matches
// the cover square, and uploads it as a Direct2D bitmap. Cached by version
// and size, so this runs once per track.
Result<void> WidgetRenderer::ensureCover(const WidgetModel& model, const RectF& zone) {
    if (model.coverImage.empty()) {
        m_cover.reset();
        return {};
    }
    const int sizePx = std::max(1, static_cast<int>(std::lround(zone.width() * static_cast<float>(m_dpi) / 96.0f)));
    if (m_cover && m_cover->version == model.coverVersion && m_cover->sizePx == sizePx) {
        return {};
    }
    m_cover.reset();

    IWICImagingFactory& wic = *m_graphics.wic;
    winrt::com_ptr<IWICStream> stream;
    HRESULT hr = wic.CreateStream(stream.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICImagingFactory::CreateStream");
    }
    // WIC wants a mutable pointer but only reads through it.
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(model.coverImage.data()),
                                      static_cast<DWORD>(model.coverImage.size()));
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICStream::InitializeFromMemory");
    }

    winrt::com_ptr<IWICBitmapDecoder> decoder;
    hr = wic.CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateDecoderFromStream(cover)");
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICBitmapDecoder::GetFrame");
    }

    UINT width = 0;
    UINT height = 0;
    frame->GetSize(&width, &height);
    if (width == 0 || height == 0) {
        return Error::fromHResult(E_UNEXPECTED, "cover has no pixels");
    }
    const double scale = static_cast<double>(sizePx) / static_cast<double>(std::min(width, height));
    const UINT scaledWidth = std::max<UINT>(1, static_cast<UINT>(std::lround(width * scale)));
    const UINT scaledHeight = std::max<UINT>(1, static_cast<UINT>(std::lround(height * scale)));

    winrt::com_ptr<IWICBitmapScaler> scaler;
    hr = wic.CreateBitmapScaler(scaler.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateBitmapScaler");
    }
    hr = scaler->Initialize(frame.get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeHighQualityCubic);
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICBitmapScaler::Initialize");
    }

    winrt::com_ptr<IWICFormatConverter> converter;
    hr = wic.CreateFormatConverter(converter.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateFormatConverter");
    }
    hr = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICFormatConverter::Initialize");
    }

    Cover cover{.version = model.coverVersion, .sizePx = sizePx};
    hr = m_target->CreateBitmapFromWicBitmap(converter.get(), nullptr, cover.bitmap.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateBitmapFromWicBitmap(cover)");
    }

    // Centre-crop to a square, in DIPs of the bitmap's own DPI (96).
    const float side = static_cast<float>(sizePx);
    const float left = (static_cast<float>(scaledWidth) - side) / 2.0f;
    const float top = (static_cast<float>(scaledHeight) - side) / 2.0f;
    cover.source = {left, top, left + side, top + side};
    m_cover = std::move(cover);
    return {};
}

Result<void> WidgetRenderer::ensureTarget(const LayeredSurface& surface, UINT dpi) {
    if (!m_target) {
        const D2D1_RENDER_TARGET_PROPERTIES properties{
            .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
            .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
            .dpiX = static_cast<float>(dpi),
            .dpiY = static_cast<float>(dpi),
            .usage = D2D1_RENDER_TARGET_USAGE_NONE,
            .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
        };
        HRESULT hr = m_graphics.d2d->CreateDCRenderTarget(&properties, m_target.put());
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "CreateDCRenderTarget");
        }
        m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // ClearType needs an opaque backdrop.

        hr = m_target->CreateSolidColorBrush(toD2D(config::titleColor), m_brush.put());
        if (FAILED(hr)) {
            releaseDeviceResources();
            return Error::fromHResult(hr, "CreateSolidColorBrush");
        }
        m_dpi = dpi;
        m_boundDc = nullptr;
    }

    if (m_dpi != dpi) {
        m_target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        m_dpi = dpi;
    }

    const SIZE size = surface.size();
    if (m_boundDc != surface.dc() || m_boundSize.cx != size.cx || m_boundSize.cy != size.cy) {
        const RECT bounds{.left = 0, .top = 0, .right = size.cx, .bottom = size.cy};
        const HRESULT hr = m_target->BindDC(surface.dc(), &bounds);
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "ID2D1DCRenderTarget::BindDC");
        }
        m_boundDc = surface.dc();
        m_boundSize = size;
    }
    return {};
}

Result<void> WidgetRenderer::draw(LayeredSurface& surface, const WidgetModel& model, const WidgetLayout& layout,
                                  UINT dpi) {
    if (const Result<void> ready = ensureTarget(surface, dpi); !ready) {
        return ready;
    }

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    m_target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    drawBackground(layout);
    drawCover(layout, model);
    drawText(layout);
    drawControls(layout, model);
    drawSpectrum(layout, model);

    const HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        releaseDeviceResources();
        return Error::fromHResult(hr, "EndDraw: target lost, will recreate");
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "ID2D1RenderTarget::EndDraw");
    }
    return {};
}

void WidgetRenderer::fill(const Color& color) {
    m_brush->SetColor(toD2D(color));
}

void WidgetRenderer::drawBackground(const WidgetLayout& layout) {
    const D2D1_ROUNDED_RECT shape{
        .rect = {0.0f, 0.0f, layout.width, layout.height},
        .radiusX = config::backgroundCornerRadiusDip,
        .radiusY = config::backgroundCornerRadiusDip,
    };
    fill(config::backgroundColor);
    m_target->FillRoundedRectangle(shape, m_brush.get());

    const D2D1_ROUNDED_RECT border{
        .rect = {0.5f, 0.5f, layout.width - 0.5f, layout.height - 0.5f},
        .radiusX = config::backgroundCornerRadiusDip,
        .radiusY = config::backgroundCornerRadiusDip,
    };
    fill(config::backgroundBorderColor);
    m_target->DrawRoundedRectangle(border, m_brush.get(), 1.0f);
}

void WidgetRenderer::drawCover(const WidgetLayout& layout, const WidgetModel& model) {
    const D2D1_ROUNDED_RECT shape{
        .rect = toD2D(layout.cover),
        .radiusX = config::coverCornerRadiusDip,
        .radiusY = config::coverCornerRadiusDip,
    };

    if (const Result<void> ready = ensureCover(model, layout.cover); !ready) {
        // Drawing continues without the image; the placeholder stands in.
        // Logged by the caller through the frame result would be noisy per
        // frame, so failures simply fall back here.
        m_cover.reset();
    }

    if (!m_cover) {
        fill(config::coverPlaceholderColor);
        m_target->FillRoundedRectangle(shape, m_brush.get());
        return;
    }

    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
    if (FAILED(m_graphics.d2d->CreateRoundedRectangleGeometry(shape, clip.put()))) {
        m_target->DrawBitmap(m_cover->bitmap.get(), shape.rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                             m_cover->source);
        return;
    }

    D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
    layer.geometricMask = clip.get();
    layer.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    m_target->PushLayer(layer, nullptr);
    m_target->DrawBitmap(m_cover->bitmap.get(), shape.rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                         m_cover->source);
    m_target->PopLayer();
}

void WidgetRenderer::drawText(const WidgetLayout& layout) {
    if (m_title.layout) {
        fill(config::titleColor);
        m_target->DrawTextLayout(D2D1_POINT_2F{layout.title.left, layout.title.top}, m_title.layout.get(),
                                 m_brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (m_artist.layout) {
        fill(config::artistColor);
        m_target->DrawTextLayout(D2D1_POINT_2F{layout.artist.left, layout.artist.top}, m_artist.layout.get(),
                                 m_brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void WidgetRenderer::drawControls(const WidgetLayout& layout, const WidgetModel& model) {
    constexpr float s = config::controlGlyphSizeDip;
    fill(config::controlColor);

    const auto place = [&](const RectF& zone) {
        const float x = zone.left + (zone.width() - s) / 2.0f;
        const float y = zone.top + (zone.height() - s) / 2.0f;
        m_target->SetTransform(D2D1::Matrix3x2F::Translation(x, y));
    };

    place(layout.previous);
    m_target->FillGeometry(m_previousGlyph.get(), m_brush.get());

    place(layout.playPause);
    if (model.playing) {
        constexpr float barWidth = 0.34f * s;
        m_target->FillRoundedRectangle(D2D1_ROUNDED_RECT{{0.0f, 0.0f, barWidth, s}, 1.0f, 1.0f}, m_brush.get());
        m_target->FillRoundedRectangle(D2D1_ROUNDED_RECT{{s - barWidth, 0.0f, s, s}, 1.0f, 1.0f}, m_brush.get());
    } else {
        m_target->FillGeometry(m_playGlyph.get(), m_brush.get());
    }

    place(layout.next);
    m_target->FillGeometry(m_nextGlyph.get(), m_brush.get());

    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void WidgetRenderer::drawSpectrum(const WidgetLayout& layout, const WidgetModel& model) {
    using namespace config;
    const RectF& zone = layout.visualizer;
    const float maxHeight = zone.height();

    fill(model.accent);
    float x = zone.left;
    for (int i = 0; i < spectrumBarCount; ++i) {
        const float value = std::clamp(model.spectrum[static_cast<std::size_t>(i)], 0.0f, 1.0f);
        const float height = spectrumBaselineDip + value * (maxHeight - spectrumBaselineDip);
        const D2D1_ROUNDED_RECT bar{
            .rect = {x, zone.bottom - height, x + spectrumBarWidthDip, zone.bottom},
            .radiusX = 1.0f,
            .radiusY = 1.0f,
        };
        m_target->FillRoundedRectangle(bar, m_brush.get());
        x += spectrumBarWidthDip + spectrumBarGapDip;
    }
}

}  // namespace threnody::render
