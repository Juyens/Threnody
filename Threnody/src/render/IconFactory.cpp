#include "render/IconFactory.h"

#include "render/LayeredSurface.h"

namespace threnody::render {

Result<win32::unique_hicon> renderAppIcon(Graphics& graphics, int sizePx) {
    LayeredSurface surface;
    if (const Result<void> resized = surface.resize(SIZE{.cx = sizePx, .cy = sizePx}); !resized) {
        return resized.error();
    }

    // Draw in a 16-unit box and let the DPI scale it to the requested size.
    const float dpi = 96.0f * static_cast<float>(sizePx) / 16.0f;
    const D2D1_RENDER_TARGET_PROPERTIES properties{
        .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
        .pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
        .dpiX = dpi,
        .dpiY = dpi,
        .usage = D2D1_RENDER_TARGET_USAGE_NONE,
        .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
    };
    winrt::com_ptr<ID2D1DCRenderTarget> target;
    HRESULT hr = graphics.d2d->CreateDCRenderTarget(&properties, target.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateDCRenderTarget(icon)");
    }
    const RECT bounds{0, 0, sizePx, sizePx};
    hr = target->BindDC(surface.dc(), &bounds);
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "BindDC(icon)");
    }
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    hr = target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, brush.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateSolidColorBrush(icon)");
    }

    target->BeginDraw();
    target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
    constexpr float heights[] = {7.0f, 12.0f, 9.0f};
    constexpr float barWidth = 3.0f;
    constexpr float gap = 1.5f;
    constexpr float totalWidth = 3.0f * barWidth + 2.0f * gap;
    float x = (16.0f - totalWidth) / 2.0f;
    for (const float height : heights) {
        const float top = 14.0f - height;
        target->FillRoundedRectangle(D2D1_ROUNDED_RECT{{x, top, x + barWidth, 14.0f}, 1.5f, 1.5f}, brush.get());
        x += barWidth + gap;
    }
    hr = target->EndDraw();
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "EndDraw(icon)");
    }

    // A 32-bit colour bitmap with alpha needs a mask bitmap only formally.
    win32::unique_hbitmap mask{CreateBitmap(sizePx, sizePx, 1, 1, nullptr)};
    if (!mask) {
        return Error::fromLastError("CreateBitmap(icon mask)");
    }
    ICONINFO info{.fIcon = TRUE, .hbmMask = mask.get(), .hbmColor = surface.bitmap()};
    win32::unique_hicon icon{CreateIconIndirect(&info)};
    if (!icon) {
        return Error::fromLastError("CreateIconIndirect");
    }
    return icon;
}

}  // namespace threnody::render
