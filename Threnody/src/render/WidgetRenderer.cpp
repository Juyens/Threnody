#include "render/WidgetRenderer.h"

#include "Config.h"
#include "color/ColorSpace.h"
#include "render/Icons.h"
#include "render/SvgPath.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace threnody::render {
namespace {

constexpr D2D1_COLOR_F toD2D(const Color& color) noexcept {
    return D2D1_COLOR_F{color.r, color.g, color.b, color.a};
}

constexpr D2D1_RECT_F toD2D(const RectF& rect) noexcept {
    return D2D1_RECT_F{rect.left, rect.top, rect.right, rect.bottom};
}

constexpr Color mix(const Color& a, const Color& b, float t) noexcept {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// Cubic ease-out of the hover fade, so it lands softly like the reference.
constexpr float eased(float t) noexcept {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

constexpr float easeInOutCubic(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
}

// One box blur pass along rows (`horizontal`) or columns of 32-bit pixels,
// edges clamped. Three passes approximate a Gaussian.
void boxBlur(std::vector<std::uint8_t>& pixels, std::vector<std::uint8_t>& scratch, int width, int height, int radius,
             bool horizontal) {
    const int lines = horizontal ? height : width;
    const int length = horizontal ? width : height;
    const auto at = [&](int line, int i) {
        return static_cast<std::size_t>(horizontal ? (line * width + i) : (i * width + line)) * 4;
    };
    const float span = static_cast<float>(2 * radius + 1);
    scratch.resize(pixels.size());
    for (int line = 0; line < lines; ++line) {
        for (int channel = 0; channel < 4; ++channel) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                sum += pixels[at(line, std::clamp(k, 0, length - 1)) + channel];
            }
            for (int i = 0; i < length; ++i) {
                scratch[at(line, i) + channel] = static_cast<std::uint8_t>(std::lround(sum / span));
                sum += pixels[at(line, std::min(i + radius + 1, length - 1)) + channel];
                sum -= pixels[at(line, std::max(i - radius, 0)) + channel];
            }
        }
    }
    pixels.swap(scratch);
}

// How far a transition started at `start` has come, in [0, 1]; 1 when none
// is running.
float progress(ULONGLONG start, unsigned durationMs) noexcept {
    if (start == 0) {
        return 1.0f;
    }
    return std::clamp(static_cast<float>(GetTickCount64() - start) / static_cast<float>(durationMs), 0.0f, 1.0f);
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

    geometry = pathGeometryFromSvg(*m_graphics.d2d, icons::shuffle);
    if (!geometry) {
        return geometry.error();
    }
    m_shuffleIcon = std::move(geometry.value());

    const std::array speakers{icons::speakerMute, icons::speaker0, icons::speaker1, icons::speaker2};
    for (std::size_t i = 0; i < speakers.size(); ++i) {
        geometry = pathGeometryFromSvg(*m_graphics.d2d, speakers[i]);
        if (!geometry) {
            return geometry.error();
        }
        m_speakerIcons[i] = std::move(geometry.value());
    }
    return {};
}

Result<void> WidgetRenderer::updateTextLine(TextLine& line, const std::wstring& text, float maxWidth,
                                            IDWriteTextFormat& format) {
    if (line.layout && line.text == text && line.maxWidth == maxWidth && line.format == &format) {
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
    line.format = &format;
    line.layout = std::move(layout);
    line.metrics = metrics;
    return {};
}

// New text: keep the old lines around to slide them out.
void WidgetRenderer::noteTextChange(const WidgetModel& model) {
    if (m_title.layout && (model.title != m_title.text || model.artist != m_artist.text)) {
        m_titleFrom = m_title;
        m_artistFrom = m_artist;
        m_textSlideStart = GetTickCount64();
    }
}

// Lays the text out at unlimited width: the natural size, for measuring and
// for scrolling. Callers then rebuild it at the width it actually gets.
Result<void> WidgetRenderer::measureText(const WidgetModel& model, IDWriteTextFormat& title, IDWriteTextFormat& artist) {
    if (const Result<void> r = updateTextLine(m_title, model.title, measureWidth, title); !r) {
        return r;
    }
    if (const Result<void> r = updateTextLine(m_artist, model.artist, measureWidth, artist); !r) {
        return r;
    }
    m_titleNatural = m_title.layout;
    m_artistNatural = m_artist.layout;
    return {};
}

Result<WidgetLayout> WidgetRenderer::layoutCard(const WidgetModel& model, float widthDip, float heightDip) {
    noteTextChange(model);
    if (const Result<void> r = measureText(model, m_fonts.cardTitle(), m_fonts.cardArtist()); !r) {
        return r.error();
    }
    const WidgetLayout result =
        WidgetLayout::computeCard(widthDip, heightDip, m_title.metrics.height, m_artist.metrics.height);
    if (const Result<void> r = updateTextLine(m_title, model.title, result.title.width(), m_fonts.cardTitle()); !r) {
        return r.error();
    }
    if (const Result<void> r = updateTextLine(m_artist, model.artist, result.artist.width(), m_fonts.cardArtist());
        !r) {
        return r.error();
    }
    return result;
}

Result<WidgetLayout> WidgetRenderer::layout(const WidgetModel& model, float heightDip) {
    noteTextChange(model);
    // Measure at unlimited width first, lay out, then rebuild the layouts at
    // the width they actually get so trimming applies.
    if (const Result<void> r = measureText(model, m_fonts.title(), m_fonts.artist()); !r) {
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
    m_cover.reset();  // Bitmaps and bitmap brushes belong to the target.
    m_coverFrom.reset();
    m_backdrop.reset();
    m_backdropFrom.reset();
    m_brush = nullptr;
    m_target = nullptr;
    m_boundDc = nullptr;
    m_boundSize = {};
}

// The first frame of the model's encoded cover. It reads straight from the
// model's bytes, so use it before the model changes.
Result<winrt::com_ptr<IWICBitmapFrameDecode>> WidgetRenderer::decodeCover(const WidgetModel& model) {
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
    return frame;
}

// Decodes the model's cover with WIC, scales it so the shorter side matches
// the cover square, and uploads it as a Direct2D bitmap. Cached by version
// and size, so this runs once per track. A different image (not just a new
// size) starts the flip from whatever was showing.
Result<void> WidgetRenderer::ensureCover(const WidgetModel& model, const RectF& zone) {
    const bool changed = model.coverImage.empty() ? m_cover.has_value()
                                                  : !m_cover || m_cover->version != model.coverVersion;
    if (changed) {
        // Mid-flip, the old face may still be showing: then only the new
        // face changes, and the turn carries on. This is what makes the
        // brief placeholder between two tracks invisible.
        if (progress(m_coverFlipStart, config::coverFlipMs) >= 0.5f) {
            m_coverFrom = std::move(m_cover);
            m_coverFlipStart = GetTickCount64();
        }
        m_cover.reset();
    }
    if (model.coverImage.empty()) {
        return {};
    }
    const int sizePx = std::max(1, static_cast<int>(std::lround(zone.width() * static_cast<float>(m_dpi) / 96.0f)));
    if (m_cover && m_cover->sizePx == sizePx) {
        return {};
    }
    // Without a bitmap this stands for the placeholder, so an image that
    // fails to decode is not retried (and re-flipped) on every frame.
    m_cover = Cover{.version = model.coverVersion, .sizePx = sizePx};

    Result<winrt::com_ptr<IWICBitmapFrameDecode>> decoded = decodeCover(model);
    if (!decoded) {
        return decoded.error();
    }
    IWICImagingFactory& wic = *m_graphics.wic;
    const winrt::com_ptr<IWICBitmapFrameDecode> frame = std::move(decoded.value());
    HRESULT hr = S_OK;

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

// A wash of the cover's colours for the floating panel (see the backdrop*
// constants). Built once per track and panel size, small, and drawn
// stretched: after the blur there is no detail left to lose. A new image
// cross-fades in.
Result<void> WidgetRenderer::ensureBackdrop(const WidgetModel& model, const WidgetLayout& layout) {
    const bool changed = model.coverImage.empty() ? m_backdrop.has_value()
                                                  : !m_backdrop || m_backdrop->version != model.coverVersion;
    if (changed) {
        if (progress(m_backdropFadeStart, config::coverFlipMs) >= 0.5f) {
            m_backdropFrom = std::move(m_backdrop);
            m_backdropFadeStart = GetTickCount64();
        }
        m_backdrop.reset();
    }
    if (model.coverImage.empty()) {
        return {};
    }
    const int widthDip = std::max(1, static_cast<int>(std::lround(layout.width)));
    const int heightDip = std::max(1, static_cast<int>(std::lround(layout.height)));
    if (m_backdrop && m_backdrop->widthDip == widthDip && m_backdrop->heightDip == heightDip) {
        return {};
    }
    m_backdrop = Backdrop{.version = model.coverVersion, .widthDip = widthDip, .heightDip = heightDip};

    Result<winrt::com_ptr<IWICBitmapFrameDecode>> decoded = decodeCover(model);
    if (!decoded) {
        return decoded.error();
    }
    IWICImagingFactory& wic = *m_graphics.wic;
    UINT coverWidth = 0;
    UINT coverHeight = 0;
    decoded.value()->GetSize(&coverWidth, &coverHeight);
    if (coverWidth == 0 || coverHeight == 0) {
        return Error::fromHResult(E_UNEXPECTED, "backdrop: cover has no pixels");
    }
    // The cover at the sample width, then the band across its middle.
    const UINT sampleWidth = config::backdropSampleWidthPx;
    const UINT sampleHeight = std::max<UINT>(
        1, static_cast<UINT>(std::lround(static_cast<double>(sampleWidth) * coverHeight / coverWidth)));
    const UINT bandHeight = std::clamp<UINT>(
        static_cast<UINT>(std::lround(sampleWidth * layout.height / layout.width * config::backdropBandStretch)), 4,
        sampleHeight);
    const WICRect band{0, static_cast<INT>((sampleHeight - bandHeight) / 2), static_cast<INT>(sampleWidth),
                       static_cast<INT>(bandHeight)};

    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::com_ptr<IWICBitmapClipper> clipper;
    winrt::com_ptr<IWICFormatConverter> converter;
    HRESULT hr = wic.CreateBitmapScaler(scaler.put());
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(decoded.value().get(), sampleWidth, sampleHeight, WICBitmapInterpolationModeFant);
    }
    if (SUCCEEDED(hr)) {
        hr = wic.CreateBitmapClipper(clipper.put());
    }
    if (SUCCEEDED(hr)) {
        hr = clipper->Initialize(scaler.get(), &band);
    }
    if (SUCCEEDED(hr)) {
        hr = wic.CreateFormatConverter(converter.put());
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(clipper.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                   0.0, WICBitmapPaletteTypeMedianCut);
    }
    const UINT stride = sampleWidth * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * bandHeight);
    if (SUCCEEDED(hr)) {
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "backdrop sampling");
    }

    std::vector<std::uint8_t> scratch;
    for (int pass = 0; pass < 3; ++pass) {
        boxBlur(pixels, scratch, static_cast<int>(sampleWidth), static_cast<int>(bandHeight), config::backdropBlurPx,
                true);
        boxBlur(pixels, scratch, static_cast<int>(sampleWidth), static_cast<int>(bandHeight), config::backdropBlurPx,
                false);
    }

    winrt::com_ptr<ID2D1Bitmap> bitmap;
    hr = m_target->CreateBitmap(
        D2D1::SizeU(sampleWidth, bandHeight), pixels.data(), stride,
        // At 96 DPI, so a pixel is a DIP whatever the target's scale.
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
                               96.0f),
        bitmap.put());
    if (SUCCEEDED(hr)) {
        const D2D1_BITMAP_BRUSH_PROPERTIES properties{D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR};
        hr = m_target->CreateBitmapBrush(bitmap.get(), properties, m_backdrop->brush.put());
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "backdrop bitmap");
    }
    // The bitmap is a pixel per DIP; stretch it over the panel.
    m_backdrop->brush->SetTransform(D2D1::Matrix3x2F::Scale(layout.width / static_cast<float>(sampleWidth),
                                                            layout.height / static_cast<float>(bandHeight)));
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

    // The marquee's clock runs from when the pointer arrived.
    if (!model.hover) {
        m_hoverSince = 0;
    } else if (m_hoverSince == 0) {
        m_hoverSince = GetTickCount64();
    }

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    m_target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    drawBackground(layout, model);
    drawPulse(layout, model);
    drawHoverHighlight(layout, model);
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

void WidgetRenderer::drawBackground(const WidgetLayout& layout, const WidgetModel& model) {
    const float hover = eased(model.hoverProgress);
    const D2D1_ROUNDED_RECT shape{
        .rect = {0.0f, 0.0f, layout.width, layout.height},
        .radiusX = layout.cornerRadius,
        .radiusY = layout.cornerRadius,
    };
    fill(model.floating ? mix(config::floatingBackgroundColor, config::floatingHoverBackgroundColor, hover)
                        : mix(config::backgroundColor, config::hoverBackgroundColor, hover));
    m_target->FillRoundedRectangle(shape, m_brush.get());
    if (model.floating) {
        drawBackdrop(layout, model, shape);
    }

    const D2D1_ROUNDED_RECT border{
        .rect = {0.5f, 0.5f, layout.width - 0.5f, layout.height - 0.5f},
        .radiusX = layout.cornerRadius,
        .radiusY = layout.cornerRadius,
    };
    fill(model.floating ? mix(config::floatingBorderColor, config::floatingHoverBorderColor, hover)
                        : mix(config::backgroundBorderColor, config::hoverBorderColor, hover));
    m_target->DrawRoundedRectangle(border, m_brush.get(), 1.0f);
}

// The blurred cover under a shade, cross-fading when the cover changes.
void WidgetRenderer::drawBackdrop(const WidgetLayout& layout, const WidgetModel& model,
                                  const D2D1_ROUNDED_RECT& shape) {
    // On failure the plain panel shows; the empty backdrop stops retries.
    static_cast<void>(ensureBackdrop(model, layout));
    const float fade = easeInOutCubic(progress(m_backdropFadeStart, config::coverFlipMs));
    float shown = 0.0f;
    if (m_backdropFrom && m_backdropFrom->brush && fade < 1.0f) {
        m_backdropFrom->brush->SetOpacity(1.0f - fade);
        m_target->FillRoundedRectangle(shape, m_backdropFrom->brush.get());
        shown = 1.0f - fade;
    }
    if (m_backdrop && m_backdrop->brush) {
        m_backdrop->brush->SetOpacity(fade);
        m_target->FillRoundedRectangle(shape, m_backdrop->brush.get());
        shown = std::max(shown, fade);
    }
    if (shown > 0.0f) {
        const Color shade = mix(config::backdropShadeColor, config::backdropHoverShadeColor, eased(model.hoverProgress));
        fill(shade.withAlpha(shade.a * shown));
        m_target->FillRoundedRectangle(shape, m_brush.get());
    }
}

// On a kick the panel takes a tint of the bars' colour and its border lights
// up in it, fading out over a fraction of a second.
void WidgetRenderer::drawPulse(const WidgetLayout& layout, const WidgetModel& model) {
    const float pulse = std::clamp(model.pulse, 0.0f, 1.0f);
    if (pulse <= 0.0f) {
        return;
    }
    const Color color = barColor(model, 0);
    const float inset = config::pulseBorderWidthDip / 2.0f;
    fill(color.withAlpha(config::pulseTintAlpha * pulse));
    m_target->FillRoundedRectangle(
        {{0.0f, 0.0f, layout.width, layout.height}, layout.cornerRadius, layout.cornerRadius},
        m_brush.get());
    fill(color.withAlpha(config::pulseBorderAlpha * pulse));
    m_target->DrawRoundedRectangle({{inset, inset, layout.width - inset, layout.height - inset},
                                    layout.cornerRadius, layout.cornerRadius},
                                   m_brush.get(), config::pulseBorderWidthDip);
}

// Rounded highlight behind whatever the pointer is over, like a transparent
// Fluent button lighting up.
void WidgetRenderer::drawHoverHighlight(const WidgetLayout& layout, const WidgetModel& model) {
    if (!model.hover) {
        return;
    }
    RectF area;
    switch (*model.hover) {
        case Zone::Shuffle: area = layout.shuffle; break;
        case Zone::Previous: area = layout.previous; break;
        case Zone::PlayPause: area = layout.playPause; break;
        case Zone::Next: area = layout.next; break;
        case Zone::Volume: area = layout.volume; break;
        case Zone::Title: area = layout.title; break;
        case Zone::Artist: area = layout.artist; break;
        default: return;
    }
    const bool control = *model.hover != Zone::Title && *model.hover != Zone::Artist;
    if (control) {
        area.top += config::controlHoverInsetDip;
        area.bottom -= config::controlHoverInsetDip;
    } else {
        area.left -= config::textHoverPaddingDip;
        area.right += config::textHoverPaddingDip;
        area.top -= 1.0f;
        area.bottom += 1.0f;
    }
    const D2D1_ROUNDED_RECT shape{toD2D(area), config::hoverHighlightRadiusDip, config::hoverHighlightRadiusDip};
    fill(config::hoverHighlightColor.withAlpha(config::hoverHighlightColor.a * eased(model.hoverProgress)));
    m_target->FillRoundedRectangle(shape, m_brush.get());
}

void WidgetRenderer::drawCover(const WidgetLayout& layout, const WidgetModel& model) {
    const D2D1_ROUNDED_RECT shape{
        .rect = toD2D(layout.cover),
        .radiusX = layout.coverCornerRadius,
        .radiusY = layout.coverCornerRadius,
    };

    // A failure leaves a bitmap-less cover, drawn as the placeholder; logging
    // it here would repeat every frame.
    static_cast<void>(ensureCover(model, layout.cover));

    // The flip: the old face narrows to an edge, the new one opens out.
    const float turn = easeInOutCubic(progress(m_coverFlipStart, config::coverFlipMs));
    if (turn >= 1.0f) {
        drawCoverFace(m_cover, shape);
        return;
    }
    const float squeeze = std::cos(turn * std::numbers::pi_v<float>);  // 1 -> 0 -> -1
    const D2D1_POINT_2F centre{(shape.rect.left + shape.rect.right) / 2.0f, (shape.rect.top + shape.rect.bottom) / 2.0f};
    m_target->SetTransform(D2D1::Matrix3x2F::Scale(std::max(std::abs(squeeze), 0.001f), 1.0f, centre));
    drawCoverFace(squeeze > 0.0f ? m_coverFrom : m_cover, shape);
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void WidgetRenderer::drawCoverFace(const std::optional<Cover>& face, const D2D1_ROUNDED_RECT& shape) {
    if (!face || !face->bitmap) {
        fill(config::coverPlaceholderColor);
        m_target->FillRoundedRectangle(shape, m_brush.get());
        return;
    }

    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
    if (FAILED(m_graphics.d2d->CreateRoundedRectangleGeometry(shape, clip.put()))) {
        m_target->DrawBitmap(face->bitmap.get(), shape.rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, face->source);
        return;
    }

    D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
    layer.geometricMask = clip.get();
    layer.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    m_target->PushLayer(layer, nullptr);
    m_target->DrawBitmap(face->bitmap.get(), shape.rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, face->source);
    m_target->PopLayer();
}

// After a track change the old lines slide up and fade while the new ones
// rise into place; everything stays inside the text column.
void WidgetRenderer::drawText(const WidgetLayout& layout) {
    const float slide = easeInOutCubic(progress(m_textSlideStart, config::textSlideMs));
    const D2D1_RECT_F column{layout.title.left, 0.0f, std::max(layout.title.right, layout.artist.right), layout.height};
    m_target->PushAxisAlignedClip(column, D2D1_ANTIALIAS_MODE_ALIASED);
    if (slide < 1.0f) {
        const float lift = -config::textSlideDip * slide;
        drawTextLine(m_titleFrom, nullptr, layout.title, config::titleColor, 1.0f - slide, lift);
        drawTextLine(m_artistFrom, nullptr, layout.artist, config::artistColor, 1.0f - slide, lift);
    }
    const float lift = config::textSlideDip * (1.0f - slide);
    drawTextLine(m_title, m_titleNatural, layout.title, config::titleColor, slide, lift);
    drawTextLine(m_artist, m_artistNatural, layout.artist, config::artistColor, slide, lift);
    m_target->PopAxisAlignedClip();
}

// How far a line that does not fit has scrolled, or a negative value when it
// is not scrolling (it fits, or the pointer is elsewhere). Zero during the
// pause before it starts: the whole line shows, faded at the edge.
float WidgetRenderer::marqueeOffset(const TextLine& line, const winrt::com_ptr<IDWriteTextLayout>& natural,
                                    float boxWidth) const noexcept {
    if (m_hoverSince == 0 || !natural || !line.layout) {
        return -1.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    natural->GetMetrics(&metrics);
    if (metrics.widthIncludingTrailingWhitespace <= boxWidth + 0.5f) {
        return -1.0f;
    }
    const auto elapsed = static_cast<float>(GetTickCount64() - m_hoverSince);
    if (elapsed < static_cast<float>(config::marqueeDelayMs)) {
        return 0.0f;
    }
    const float travelled =
        (elapsed - static_cast<float>(config::marqueeDelayMs)) / 1000.0f * config::marqueeSpeedDipPerSecond;
    return std::fmod(travelled, metrics.widthIncludingTrailingWhitespace + config::marqueeGapDip);
}

void WidgetRenderer::drawTextLine(const TextLine& line, const winrt::com_ptr<IDWriteTextLayout>& natural,
                                  const RectF& box, const Color& color, float opacity, float lift) {
    if (!line.layout || opacity <= 0.0f) {
        return;
    }
    fill(color.withAlpha(color.a * opacity));
    const float y = box.top + lift;
    const float offset = marqueeOffset(line, natural, box.width());
    if (offset < 0.0f) {
        m_target->DrawTextLayout({box.left, y}, line.layout.get(), m_brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return;
    }

    // Scrolling: the untrimmed line twice, one gap apart, so it loops; the
    // edges fade instead of cutting letters (the left one once it moves).
    const float fade = std::min(config::marqueeFadeDip / box.width(), 0.5f);
    const D2D1_GRADIENT_STOP stops[] = {
        {0.0f, {0.0f, 0.0f, 0.0f, offset > 0.0f ? 0.0f : 1.0f}},
        {fade, {0.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f - fade, {0.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 0.0f, 0.0f}},
    };
    winrt::com_ptr<ID2D1GradientStopCollection> collection;
    winrt::com_ptr<ID2D1LinearGradientBrush> mask;
    if (FAILED(m_target->CreateGradientStopCollection(stops, 4, collection.put())) ||
        FAILED(m_target->CreateLinearGradientBrush({{box.left, 0.0f}, {box.right, 0.0f}}, collection.get(),
                                                   mask.put()))) {
        m_target->DrawTextLayout({box.left, y}, line.layout.get(), m_brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return;
    }
    DWRITE_TEXT_METRICS metrics{};
    natural->GetMetrics(&metrics);
    const float period = metrics.widthIncludingTrailingWhitespace + config::marqueeGapDip;

    D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters({box.left, y, box.right, y + box.height()});
    layer.opacityBrush = mask.get();
    m_target->PushLayer(layer, nullptr);
    m_target->DrawTextLayout({box.left - offset, y}, natural.get(), m_brush.get());
    m_target->DrawTextLayout({box.left - offset + period, y}, natural.get(), m_brush.get());
    m_target->PopLayer();
}

bool WidgetRenderer::animating(const WidgetModel& model, const WidgetLayout& layout) const noexcept {
    if (progress(m_coverFlipStart, config::coverFlipMs) < 1.0f ||
        progress(m_textSlideStart, config::textSlideMs) < 1.0f ||
        (model.floating && progress(m_backdropFadeStart, config::coverFlipMs) < 1.0f) ||
        volumeOsdOpacity(model) > 0.0f) {
        return true;
    }
    return model.hover && (marqueeOffset(m_title, m_titleNatural, layout.title.width()) >= 0.0f ||
                           marqueeOffset(m_artist, m_artistNatural, layout.artist.width()) >= 0.0f);
}

void WidgetRenderer::drawControls(const WidgetLayout& layout, const WidgetModel& model) {
    constexpr float s = config::controlGlyphSizeDip;  // Glyph space; scaled on placement.
    const float k = layout.controlScale;
    fill(config::controlColor);

    const auto place = [&](const RectF& zone) {
        const float x = zone.left + (zone.width() - s * k) / 2.0f;
        const float y = zone.top + (zone.height() - s * k) / 2.0f;
        m_target->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y));
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

    // Fluent icons live in a 20-unit box; scale it to the icon size.
    const float icon = config::controlIconSizeDip * k;
    const auto placeIcon = [&](const RectF& zone) {
        const float x = zone.left + (zone.width() - icon) / 2.0f;
        const float y = zone.top + (zone.height() - icon) / 2.0f;
        const float scale = icon / icons::canvasUnits;
        m_target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
        return D2D1_POINT_2F{x + icon / 2.0f, y + icon};
    };

    // Shuffle: green with a dot underneath while on, as in Spotify. The icon
    // stays in line with the other controls; the arrows end well above the
    // box's bottom edge, so the dot fits just under them.
    const bool shuffleOn = model.shuffle.value_or(false);
    fill(!model.shuffle ? config::controlDisabledColor : shuffleOn ? config::controlActiveColor : config::controlColor);
    const D2D1_POINT_2F below = placeIcon(layout.shuffle);
    m_target->FillGeometry(m_shuffleIcon.get(), m_brush.get());
    if (shuffleOn) {
        m_target->SetTransform(D2D1::Matrix3x2F::Identity());
        const float r = config::controlActiveDotDip * k / 2.0f;
        m_target->FillEllipse(D2D1::Ellipse({below.x, below.y + r - k}, r, r), m_brush.get());
    }

    // Volume: waves follow the level, a cross when muted or at zero.
    const float volume = model.volume.value_or(1.0f);
    const std::size_t speaker = volume <= 0.0f ? 0 : volume < 0.34f ? 1 : volume < 0.67f ? 2 : 3;
    fill(model.volume ? config::controlColor : config::controlDisabledColor);
    placeIcon(layout.volume);
    m_target->FillGeometry(m_speakerIcons[speaker].get(), m_brush.get());

    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

Color WidgetRenderer::barColor(const WidgetModel& model, int bar) {
    using namespace config;
    if (model.colorMode == ColorMode::Rainbow) {
        const float hue = 360.0f * model.rainbowPhase + static_cast<float>(bar) * rainbowHueSpanDegrees / spectrumBarCount;
        return color::fromOklch({rainbowLightness, rainbowChroma, hue});
    }
    if (model.colorMode == ColorMode::TrackGradient) {
        const float t = 2.0f * std::numbers::pi_v<float> *
                        (model.rainbowPhase + static_cast<float>(bar) * gradientWaveSpan / spectrumBarCount);
        const float wave = std::cos(t);  // +1 crest (light tint), -1 trough (deep shade).
        color::Oklch lch = color::toOklch(model.accent);
        lch.l = std::clamp(lch.l + gradientLightnessSpread * wave, gradientMinLightness, gradientMaxLightness);
        lch.c *= 1.0f - gradientChromaFade * std::max(wave, 0.0f);
        lch.h += gradientHueSpreadDegrees * wave;
        return color::fromOklch(lch);
    }
    return model.accent;
}

float WidgetRenderer::volumeOsdOpacity(const WidgetModel& model) noexcept {
    if (model.volumeOsdSince == 0 || !model.volume) {
        return 0.0f;
    }
    const auto elapsed = static_cast<float>(GetTickCount64() - model.volumeOsdSince);
    const float hold = static_cast<float>(config::volumeOsdHoldMs);
    return elapsed <= hold ? 1.0f : std::clamp(1.0f - (elapsed - hold) / config::volumeOsdFadeMs, 0.0f, 1.0f);
}

void WidgetRenderer::drawSpectrum(const WidgetLayout& layout, const WidgetModel& model) {
    using namespace config;
    const RectF& zone = layout.visualizer;
    const float maxHeight = zone.height();

    // While the wheel changes the volume, the level takes the bars' place.
    const float osd = volumeOsdOpacity(model);
    if (osd > 0.0f) {
        const float level = std::clamp(model.volume.value_or(0.0f), 0.0f, 1.0f);
        const std::wstring value = std::to_wstring(static_cast<int>(std::lround(level * 100.0f)));
        winrt::com_ptr<IDWriteTextLayout> text;
        if (SUCCEEDED(m_graphics.dwrite->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()),
                                                          &m_fonts.artist(), zone.width(), zone.height(), text.put()))) {
            text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            DWRITE_TEXT_METRICS metrics{};
            text->GetMetrics(&metrics);
            fill(config::titleColor.withAlpha(config::titleColor.a * osd));
            const float textTop = zone.top + (zone.height() - volumeOsdTrackHeightDip - 3.0f - metrics.height) / 2.0f;
            m_target->DrawTextLayout({zone.left, textTop}, text.get(), m_brush.get());
        }
        const float half = volumeOsdTrackHeightDip / 2.0f;
        const float trackY = zone.bottom - half;
        fill(config::volumeFlyoutTrackColor.withAlpha(config::volumeFlyoutTrackColor.a * osd));
        m_target->FillRoundedRectangle({{zone.left, trackY - half, zone.right, trackY + half}, half, half}, m_brush.get());
        fill(barColor(model, 0).withAlpha(osd));
        m_target->FillRoundedRectangle(
            {{zone.left, trackY - half, zone.left + zone.width() * level, trackY + half}, half, half}, m_brush.get());
        if (osd >= 1.0f) {
            return;
        }
    }

    // Bar and gap keep their proportions across the zone's width: the bar's
    // natural size in the taskbar, wider in the card.
    const float natural = spectrumBarCount * spectrumBarWidthDip + (spectrumBarCount - 1) * spectrumBarGapDip;
    const float spread = zone.width() / natural;
    const float barWidth = spectrumBarWidthDip * spread;
    const float radius = std::min(barWidth / 2.0f, 1.0f * spread);
    float x = zone.left;
    for (int i = 0; i < spectrumBarCount; ++i) {
        const Color color = barColor(model, i);
        fill(color.withAlpha(color.a * (1.0f - osd)));
        const float value = std::clamp(model.spectrum[static_cast<std::size_t>(i)], 0.0f, 1.0f);
        const float height = spectrumBaselineDip + value * (maxHeight - spectrumBaselineDip);
        const D2D1_ROUNDED_RECT bar{
            .rect = {x, zone.bottom - height, x + barWidth, zone.bottom},
            .radiusX = radius,
            .radiusY = radius,
        };
        m_target->FillRoundedRectangle(bar, m_brush.get());
        x += (spectrumBarWidthDip + spectrumBarGapDip) * spread;
    }
}

}  // namespace threnody::render
