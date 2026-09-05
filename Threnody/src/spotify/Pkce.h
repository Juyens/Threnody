#pragma once

#include "util/Result.h"

#include <string>
#include <string_view>

// Proof Key for Code Exchange: a random verifier and its SHA-256 challenge,
// both base64url without padding, as RFC 7636 wants them.
namespace threnody::spotify::pkce {

[[nodiscard]] Result<std::string> randomToken(std::size_t bytes = 48);
[[nodiscard]] Result<std::string> challenge(std::string_view verifier);
[[nodiscard]] std::string base64Url(const unsigned char* data, std::size_t size);

}  // namespace threnody::spotify::pkce
