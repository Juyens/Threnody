#pragma once

#include "util/Result.h"

#include <unknwn.h>
#include <d2d1_1.h>
#include <winrt/base.h>

#include <string_view>

namespace threnody::render {

// Builds a Direct2D path geometry from SVG path data (the `d` attribute).
// Supports M/m, L/l, H/h, V/v, C/c, S/s, Q/q, A/a and Z/z with implicit
// command repetition. SVG's endpoint arc parameterisation maps one-to-one
// onto D2D1_ARC_SEGMENT, so arcs need no conversion.
[[nodiscard]] Result<winrt::com_ptr<ID2D1PathGeometry>> pathGeometryFromSvg(
    ID2D1Factory1& factory, std::string_view pathData, D2D1_FILL_MODE fillMode = D2D1_FILL_MODE_WINDING);

}  // namespace threnody::render
