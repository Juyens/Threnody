#pragma once

#include <Windows.h>

#include <string_view>

namespace threnody::shell {

// Puts `text` on the clipboard as Unicode text; false if it could not.
bool copyText(HWND owner, std::wstring_view text);

}  // namespace threnody::shell
