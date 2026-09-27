#pragma once

#include <array>
#include <string_view>

// Icon outlines taken verbatim from Lucide (https://lucide.dev, ISC; see
// THIRD_PARTY_NOTICES.md), version 1.48. Lucide icons are strokes, not fills:
// each entry is one <path> of the icon, in a 24 x 24 box, drawn with
// render::pathGeometryFromSvg and stroked strokeUnits wide with round caps
// and joins, as Lucide itself renders them. The only conversion is pause's
// two <rect> elements, written out as the equivalent rounded-rectangle paths.
namespace threnody::render::icons {

inline constexpr float canvasUnits = 24.0f;
inline constexpr float strokeUnits = 2.0f;

// lucide/shuffle
inline constexpr std::array<std::string_view, 5> shuffle{
    "m18 14 4 4-4 4",
    "m18 2 4 4-4 4",
    "M2 18h1.973a4 4 0 0 0 3.3-1.7l5.454-8.6a4 4 0 0 1 3.3-1.7H22",
    "M2 6h1.972a4 4 0 0 1 3.6 2.2",
    "M22 18h-6.041a4 4 0 0 1-3.3-1.8l-.359-.45",
};

// lucide/sparkle
inline constexpr std::array<std::string_view, 1> sparkle{
    "M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l"
    "-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1"
    ".594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z",
};

// lucide/repeat
inline constexpr std::array<std::string_view, 4> repeat{
    "m17 2 4 4-4 4",
    "M3 11v-1a4 4 0 0 1 4-4h14",
    "m7 22-4-4 4-4",
    "M21 13v1a4 4 0 0 1-4 4H3",
};

// lucide/repeat-1
inline constexpr std::array<std::string_view, 5> repeatOne{
    "m17 2 4 4-4 4",
    "M3 11v-1a4 4 0 0 1 4-4h14",
    "m7 22-4-4 4-4",
    "M21 13v1a4 4 0 0 1-4 4H3",
    "M11 10h1v4",
};

// lucide/skip-back
inline constexpr std::array<std::string_view, 2> previous{
    "M17.971 4.285A2 2 0 0 1 21 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432z",
    "M3 20V4",
};

// lucide/play
inline constexpr std::array<std::string_view, 1> play{
    "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z",
};

// lucide/pause
inline constexpr std::array<std::string_view, 2> pause{
    // <rect x="14" y="3" width="5" height="18" rx="1" />
    "M15 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1 -1 1h-3a1 1 0 0 1 -1 -1v-16a1 1 0 0 1 1 -1z",
    // <rect x="5" y="3" width="5" height="18" rx="1" />
    "M6 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1 -1 1h-3a1 1 0 0 1 -1 -1v-16a1 1 0 0 1 1 -1z",
};

// lucide/skip-forward
inline constexpr std::array<std::string_view, 2> next{
    "M21 4v16",
    "M6.029 4.285A2 2 0 0 0 3 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432z",
};

// lucide/volume-x
inline constexpr std::array<std::string_view, 3> speakerMute{
    "M11 4.702a.7.7 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 "
    "1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.7.7 0 0 0 11 19.298z",
    "m16.5 14.5 5-5",
    "m16.5 9.5 5 5",
};

// lucide/volume
inline constexpr std::array<std::string_view, 1> speaker0{
    "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 "
    "0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z",
};

// lucide/volume-1
inline constexpr std::array<std::string_view, 2> speaker1{
    "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 "
    "0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z",
    "M16 9a5 5 0 0 1 0 6",
};

// lucide/volume-2
inline constexpr std::array<std::string_view, 3> speaker2{
    "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 "
    "0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z",
    "M16 9a5 5 0 0 1 0 6",
    "M19.364 18.364a9 9 0 0 0 0-12.728",
};

}  // namespace threnody::render::icons
