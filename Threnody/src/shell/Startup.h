#pragma once

#include "util/Result.h"

namespace threnody::shell {

// The per-user Run key entry that launches Threnody at sign-in.
[[nodiscard]] bool isStartWithWindowsEnabled();
[[nodiscard]] Result<void> setStartWithWindows(bool enabled);

}  // namespace threnody::shell
