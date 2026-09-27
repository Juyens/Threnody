#pragma once

namespace threnody {

// Spotify's three repeat states, in the order its button cycles them.
enum class RepeatMode { Off, All, One };

[[nodiscard]] constexpr RepeatMode nextRepeatMode(RepeatMode mode) noexcept {
    switch (mode) {
        case RepeatMode::Off: return RepeatMode::All;
        case RepeatMode::All: return RepeatMode::One;
        case RepeatMode::One: return RepeatMode::Off;
    }
    return RepeatMode::Off;
}

}  // namespace threnody
