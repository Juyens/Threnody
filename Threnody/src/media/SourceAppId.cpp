#include "media/SourceAppId.h"

#include <algorithm>
#include <cwctype>

namespace threnody::media {
namespace {

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

}  // namespace

std::wstring processNameFromSourceAppId(std::wstring_view sourceAppId) {
    if (const std::size_t bang = sourceAppId.rfind(L'!'); bang != std::wstring_view::npos) {
        return std::wstring{sourceAppId.substr(bang + 1)};
    }

    if (const std::size_t slash = sourceAppId.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
        sourceAppId.remove_prefix(slash + 1);
    }
    constexpr std::wstring_view extension = L".exe";
    if (sourceAppId.size() > extension.size() &&
        equalsIgnoreCase(sourceAppId.substr(sourceAppId.size() - extension.size()), extension)) {
        sourceAppId.remove_suffix(extension.size());
    }
    return std::wstring{sourceAppId};
}

bool isSpotifySource(std::wstring_view sourceAppId) {
    return equalsIgnoreCase(processNameFromSourceAppId(sourceAppId), L"Spotify");
}

}  // namespace threnody::media
