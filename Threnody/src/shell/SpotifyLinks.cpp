#include "shell/SpotifyLinks.h"

#include "util/Log.h"
#include "util/Text.h"

#include <Windows.h>
#include <shellapi.h>

#include <format>

namespace threnody::shell {

std::wstring percentEncode(std::wstring_view text) {
    const std::string utf8 = text::toUtf8(text);
    std::wstring encoded;
    encoded.reserve(utf8.size() * 3);
    for (const unsigned char byte : utf8) {
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
                                byte == '~';
        if (unreserved) {
            encoded.push_back(static_cast<wchar_t>(byte));
        } else {
            encoded += std::format(L"%{:02X}", byte);
        }
    }
    return encoded;
}

void openSpotifyUri(std::wstring_view uri) {
    const std::wstring target{uri};
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        log::warn("opening {} failed (ShellExecute code {})", text::toUtf8(target), result);
    } else {
        log::info("opened {}", text::toUtf8(target));
    }
}

void openSpotifySearch(std::wstring_view query) {
    openSpotifyUri(L"spotify:search:" + percentEncode(query));
}

}  // namespace threnody::shell
