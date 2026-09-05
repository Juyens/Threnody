#pragma once

#include <string>
#include <string_view>

namespace threnody::shell {

// Percent-encodes UTF-16 text as UTF-8 for use inside a URI: unreserved
// characters pass, everything else becomes %XX.
[[nodiscard]] std::wstring percentEncode(std::wstring_view text);

// Opens a Spotify URI ("spotify:...") in the desktop app.
void openSpotifyUri(std::wstring_view uri);

// SMTC gives titles and artists as text, not Spotify ids, so without the Web
// API the best target is the in-app search for that text.
void openSpotifySearch(std::wstring_view query);

}  // namespace threnody::shell
