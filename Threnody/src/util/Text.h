#pragma once

#include <string>
#include <string_view>

namespace threnody::text {

// UTF-16 <-> UTF-8 conversions over WideCharToMultiByte / MultiByteToWideChar.
// Invalid sequences are replaced, never thrown on.
[[nodiscard]] std::string toUtf8(std::wstring_view text);
[[nodiscard]] std::wstring toWide(std::string_view text);

}  // namespace threnody::text
