#pragma once

#include "render/Graphics.h"
#include "util/Result.h"
#include "util/Win32.h"

namespace threnody::render {

// Draws the application icon at runtime: three white bars echoing the
// visualiser, on a transparent square. No .ico resource to maintain.
[[nodiscard]] Result<win32::unique_hicon> renderAppIcon(Graphics& graphics, int sizePx);

}  // namespace threnody::render
