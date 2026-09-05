#include "render/Graphics.h"

namespace threnody::render {

Result<Graphics> Graphics::create() {
    Graphics graphics;

    const D2D1_FACTORY_OPTIONS options{
#ifdef _DEBUG
        .debugLevel = D2D1_DEBUG_LEVEL_INFORMATION,
#else
        .debugLevel = D2D1_DEBUG_LEVEL_NONE,
#endif
    };
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                   graphics.d2d.put_void());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "D2D1CreateFactory");
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                             reinterpret_cast<IUnknown**>(graphics.dwrite.put()));
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "DWriteCreateFactory");
    }

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                          graphics.wic.put_void());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CoCreateInstance(WICImagingFactory)");
    }

    return graphics;
}

}  // namespace threnody::render
