#pragma once

#include "util/Result.h"

#include <unknwn.h>
#include <wincodec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace threnody::render {

// Decodes an encoded cover and returns it as a small `size` x `size` 32-bit
// BGRA raster, for colour analysis. Straight alpha, row-major, top-down.
[[nodiscard]] Result<std::vector<std::uint32_t>> sampleCover(IWICImagingFactory& wic,
                                                             std::span<const std::uint8_t> encoded, UINT size);

}  // namespace threnody::render
