#pragma once

#include <string>
#include <string_view>

namespace threnody::media {

// SMTC identifies a session by SourceAppUserModelId. For packaged apps that is
// an AUMID such as "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"; for desktop
// apps it is an executable name such as "Spotify.exe". Treating the AUMID as a
// file name yields "SpotifyAB", which matches no process, so the segment after
// '!' is used instead.
[[nodiscard]] std::wstring processNameFromSourceAppId(std::wstring_view sourceAppId);

[[nodiscard]] bool isSpotifySource(std::wstring_view sourceAppId);

}  // namespace threnody::media
