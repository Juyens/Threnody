#pragma once

#include "util/Result.h"

#include <filesystem>

namespace threnody::paths {

// %LOCALAPPDATA%\Threnody, created on first use.
[[nodiscard]] Result<std::filesystem::path> dataDirectory();

}  // namespace threnody::paths
