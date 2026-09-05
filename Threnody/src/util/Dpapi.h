#pragma once

#include "util/Result.h"

#include <string>
#include <string_view>

// Secrets at rest: encrypted for the current user with DPAPI and stored as
// base64 text, so they can live inside the JSON settings file.
namespace threnody::dpapi {

[[nodiscard]] Result<std::string> protect(std::string_view secret);
[[nodiscard]] Result<std::string> unprotect(std::string_view protectedBase64);

}  // namespace threnody::dpapi
