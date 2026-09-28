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

template <std::size_t N>
Result<std::vector<winrt::com_ptr<ID2D1PathGeometry>>> buildIcon(ID2D1Factory1& factory,
                                                                 const std::array<std::string_view, N>& paths) {
    std::vector<winrt::com_ptr<ID2D1PathGeometry>> icon;
    for (const std::string_view path : paths) {
        Result<winrt::com_ptr<ID2D1PathGeometry>> geometry = pathGeometryFromSvg(factory, path);
        if (!geometry) {
            return geometry.error();
        }
        icon.push_back(std::move(geometry.value()));
    }
    return icon;
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
    if (const Result<void> icons = renderer->ensureIcons(); !icons) {
        return icons.error();
    }
    return renderer;
}

Result<void> WidgetRenderer::ensureIcons() {
    const D2D1_STROKE_STYLE_PROPERTIES round{
        .startCap = D2D1_CAP_STYLE_ROUND,
        .endCap = D2D1_CAP_STYLE_ROUND,
        .dashCap = D2D1_CAP_STYLE_ROUND,
        .lineJoin = D2D1_LINE_JOIN_ROUND,
        .miterLimit = 10.0f,
        .dashStyle = D2D1_DASH_STYLE_SOLID,
    };
    if (const HRESULT hr = m_graphics.d2d->CreateStrokeStyle(round, nullptr, 0, m_iconStroke.put()); FAILED(hr)) {
        return Error::fromHResult(hr, "CreateStrokeStyle(icons)");
    }

    ID2D1Factory1& factory = *m_graphics.d2d;
    const auto build = [&](Icon& target, const auto& paths) -> Result<void> {
        Result<Icon> icon = buildIcon(factory, paths);
        if (!icon) {
            return icon.error();
        }
        target = std::move(icon.value());
        return {};
    };
    for (Result<void> built : {build(m_shuffleIcon, icons::shuffle), build(m_sparkleIcon, icons::sparkle),
                               build(m_repeatIcon, icons::repeat), build(m_repeatOneIcon, icons::repeatOne),
                               build(m_previousIcon, icons::previous), build(m_playIcon, icons::play),
                               build(m_pauseIcon, icons::pause), build(m_nextIcon, icons::next),
                               build(m_speakerIcons[0], icons::speakerMute), build(m_speakerIcons[1], icons::speaker0),
                               build(m_speakerIcons[2], icons::speaker1), build(m_speakerIcons[3], icons::speaker2)}) {
        if (!built) {
            return built;
        }
    }
    return {};
}

void WidgetRenderer::drawIcon(const Icon& icon, D2D1_POINT_2F origin, float size) {
    const float scale = size / icons::canvasUnits;
    m_target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(origin.x, origin.y));
    for (const auto& path : icon) {
        m_target->DrawGeometry(path.get(), m_brush.get(), icons::strokeUnits, m_iconStroke.get());
    }
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
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

Result<WidgetLayout> WidgetRenderer::layoutCard(const WidgetModel& model) {
    noteTextChange(model);
    if (const Result<void> r = measureText(model, m_fonts.cardTitle(), m_fonts.cardArtist()); !r) {
        return r.error();
    }
    const WidgetLayout result = WidgetLayout::computeCard(m_title.metrics.height, m_artist.metrics.height);
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
    if (layout.card) {
        drawCardVolume(layout, model);
    }
    drawText(layout);
    drawControls(layout, model);
    drawSeparator(layout);
    drawSpectrum(layout, model);
    drawProgress(layout, model);

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
    const bool backdrop = model.floating && !model.coverImage.empty();
    if (model.floating) {
        drawBackdrop(layout, model, shape);
    }
    // A hint of the cover's colour, where no blurred cover already gives it.
    const bool tinted = !backdrop && !model.coverImage.empty();
    if (tinted) {
        const float alpha = config::panelTintAlpha + (config::panelHoverTintAlpha - config::panelTintAlpha) * hover;
        fill(model.accent.withAlpha(alpha));
        m_target->FillRoundedRectangle(shape, m_brush.get());
    }

    const D2D1_ROUNDED_RECT border{
        .rect = {0.5f, 0.5f, layout.width - 0.5f, layout.height - 0.5f},
        .radiusX = layout.cornerRadius,
        .radiusY = layout.cornerRadius,
    };
    fill(model.floating ? mix(config::floatingBorderColor, config::floatingHoverBorderColor, hover)
                        : mix(config::backgroundBorderColor, config::hoverBorderColor, hover));
    m_target->DrawRoundedRectangle(border, m_brush.get(), 1.0f);
    if (tinted) {
        fill(model.accent.withAlpha(config::panelBorderTintAlpha));
        m_target->DrawRoundedRectangle(border, m_brush.get(), 1.0f);
    }
}

// A thin vertical rule between the controls and the visualiser (bar only).
void WidgetRenderer::drawSeparator(const WidgetLayout& layout) {
    if (layout.card) {
        return;
    }
    const float x = std::round((layout.repeat.right + layout.visualizer.left) / 2.0f) + 0.5f;
    const float half = layout.height * config::separatorHeightShare / 2.0f;
    fill(config::separatorColor);
    m_target->DrawLine({x, layout.height / 2.0f - half}, {x, layout.height / 2.0f + half}, m_brush.get(), 1.0f);
}

// The song's progress along the bottom edge, clipped to the panel's corners.
void WidgetRenderer::drawProgress(const WidgetLayout& layout, const WidgetModel& model) {
    if (model.progress < 0.0f) {
        return;
    }
    const D2D1_ROUNDED_RECT shape{{0.0f, 0.0f, layout.width, layout.height}, layout.cornerRadius,
                                  layout.cornerRadius};
    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
    if (FAILED(m_graphics.d2d->CreateRoundedRectangleGeometry(shape, clip.put()))) {
        return;
    }
    D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
    layer.geometricMask = clip.get();
    m_target->PushLayer(layer, nullptr);
    const float top = layout.height - config::progressHeightDip;
    fill(config::progressBaseColor);
    m_target->FillRectangle({0.0f, top, layout.width, layout.height}, m_brush.get());
    fill(config::progressTrackColor);
    m_target->FillRectangle({0.0f, top, layout.width, layout.height}, m_brush.get());
    // The same colour as the beat glow, so it follows the colour mode
    // (the rainbow sweep, the gradient wave, or the cover's colour).
    fill(barColor(model, 0));
    m_target->FillRectangle({0.0f, top, layout.width * std::clamp(model.progress, 0.0f, 1.0f), layout.height},
                            m_brush.get());
    m_target->PopLayer();
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
        case Zone::Repeat: area = layout.repeat; break;
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

// The card's volume indicator: a capsule over the bottom of the cover with
// the speaker, a level track and the percentage, rising a little as it
// fades in and sinking back as it fades out.
void WidgetRenderer::drawCardVolume(const WidgetLayout& layout, const WidgetModel& model) {
    using namespace config;
    const float osd = volumeOsdOpacity(model);
    if (osd <= 0.0f) {
        return;
    }
    const float level = std::clamp(model.volume.value_or(0.0f), 0.0f, 1.0f);
    const RectF& cover = layout.cover;
    const float width = cover.width() * cardOsdWidthShare;
    const float left = cover.left + (cover.width() - width) / 2.0f;
    const float bottom = cover.bottom - cardOsdMarginDip + cardOsdLiftDip * (1.0f - osd);
    const D2D1_RECT_F pill{left, bottom - cardOsdHeightDip, left + width, bottom};
    const float round = cardOsdHeightDip / 2.0f;
    const float centreY = (pill.top + pill.bottom) / 2.0f;

    fill(cardOsdBackgroundColor.withAlpha(cardOsdBackgroundColor.a * osd));
    m_target->FillRoundedRectangle({pill, round, round}, m_brush.get());

    // Speaker, drawn like the volume button's.
    const std::size_t speaker = level <= 0.0f ? 0 : level < 0.34f ? 1 : level < 0.67f ? 2 : 3;
    const float iconLeft = pill.left + round - cardOsdIconDip / 2.0f + 2.0f;
    fill(controlColor.withAlpha(controlColor.a * osd));
    drawIcon(m_speakerIcons[speaker], {iconLeft, centreY - cardOsdIconDip / 2.0f}, cardOsdIconDip);

    // Level track between the speaker and the number.
    const float trackLeft = iconLeft + cardOsdIconDip + 8.0f;
    const float trackRight = pill.right - round / 2.0f - cardOsdValueDip - 6.0f;
    const float half = volumeOsdTrackHeightDip / 2.0f;
    fill(levelTrackColor.withAlpha(levelTrackColor.a * osd));
    m_target->FillRoundedRectangle({{trackLeft, centreY - half, trackRight, centreY + half}, half, half},
                                   m_brush.get());
    fill(levelFillColor.withAlpha(levelFillColor.a * osd));
    m_target->FillRoundedRectangle(
        {{trackLeft, centreY - half, trackLeft + (trackRight - trackLeft) * level, centreY + half}, half, half},
        m_brush.get());

    const std::wstring value = std::to_wstring(static_cast<int>(std::lround(level * 100.0f)));
    winrt::com_ptr<IDWriteTextLayout> text;
    if (SUCCEEDED(m_graphics.dwrite->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()),
                                                      &m_fonts.artist(), cardOsdValueDip, cardOsdHeightDip,
                                                      text.put()))) {
        text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        DWRITE_TEXT_METRICS metrics{};
        text->GetMetrics(&metrics);
        fill(titleColor.withAlpha(titleColor.a * osd));
        m_target->DrawTextLayout({pill.right - round / 2.0f - cardOsdValueDip, centreY - metrics.height / 2.0f},
                                 text.get(), m_brush.get());
    }
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
    const float k = layout.controlScale;
    const float icon = config::controlIconSizeDip * k;
    // `share` of the icon size, `offset` (in icon sizes) from the zone's
    // centred box. The stroke keeps its width whatever the share.
    const auto drawIn = [&](const RectF& zone, const Icon& shape, float share = 1.0f, D2D1_POINT_2F offset = {}) {
        const float x = zone.left + (zone.width() - icon) / 2.0f + offset.x * icon;
        const float y = zone.top + (zone.height() - icon) / 2.0f + offset.y * icon;
        const float scale = icon * share / icons::canvasUnits;
        m_target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
        for (const auto& path : shape) {
            m_target->DrawGeometry(path.get(), m_brush.get(), icons::strokeUnits / share, m_iconStroke.get());
        }
        m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    };
    // Green with a dot underneath while on, as in Spotify.
    const auto drawDot = [&](const RectF& zone) {
        const float r = config::controlActiveDotDip * k / 2.0f;
        const float x = zone.left + zone.width() / 2.0f;
        const float y = zone.top + (zone.height() + icon) / 2.0f + r + 0.5f * k;
        m_target->FillEllipse(D2D1::Ellipse({x, y}, r, r), m_brush.get());
    };
    const auto stateColor = [](bool available, bool on) {
        return !available ? config::controlDisabledColor : on ? config::controlActiveColor : config::controlOffColor;
    };

    fill(config::controlColor);
    drawIn(layout.previous, m_previousIcon);
    drawIn(layout.playPause, model.playing ? m_pauseIcon : m_playIcon);
    drawIn(layout.next, m_nextIcon);

    // Shuffle; smart shuffle adds a sparkle, the arrows making room for it.
    const bool shuffleOn = model.shuffle.value_or(false);
    fill(stateColor(model.shuffle.has_value(), shuffleOn));
    if (shuffleOn && model.smartShuffle) {
        const float arrows = config::smartShuffleArrowsShare;
        drawIn(layout.shuffle, m_shuffleIcon, arrows, {1.0f - arrows, (1.0f - arrows) / 2.0f});
        drawIn(layout.shuffle, m_sparkleIcon, config::smartShuffleSparkleShare, {-0.08f, -0.1f});
    } else {
        drawIn(layout.shuffle, m_shuffleIcon);
    }
    if (shuffleOn) {
        drawDot(layout.shuffle);
    }

    // Repeat: off, the whole list, or one track (the icon with a 1).
    const RepeatMode repeat = model.repeat.value_or(RepeatMode::Off);
    fill(stateColor(model.repeat.has_value(), repeat != RepeatMode::Off));
    drawIn(layout.repeat, repeat == RepeatMode::One ? m_repeatOneIcon : m_repeatIcon);
    if (repeat != RepeatMode::Off) {
        drawDot(layout.repeat);
    }
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
    if (layout.card) {
        return;  // The card has controls only.
    }
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
        fill(config::levelTrackColor.withAlpha(config::levelTrackColor.a * osd));
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
