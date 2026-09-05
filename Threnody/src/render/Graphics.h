#pragma once

#include "util/Result.h"

#include <unknwn.h>
#include <d2d1_1.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <winrt/base.h>

namespace threnody::render {

// The device-independent factories, created once per process.
struct Graphics {
    winrt::com_ptr<ID2D1Factory1> d2d;
    winrt::com_ptr<IDWriteFactory2> dwrite;
    winrt::com_ptr<IWICImagingFactory> wic;

    [[nodiscard]] static Result<Graphics> create();
};

}  // namespace threnody::render
