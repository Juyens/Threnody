#pragma once

#include "Config.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace threnody::dsp {

// Finds kicks in the bass bars and turns them into a pulse in [0, 1] that
// jumps to 1 on a beat and fades out. A beat is the bass rising well above
// its own recent average, loud enough to matter, not too soon after the last
// one; comparing against the average keeps a constantly loud bass line from
// pulsing all the time.
class BeatDetector {
public:
    // One visualiser frame: the current bar levels, `elapsedMs` since the
    // previous frame. Returns the pulse to draw.
    float update(std::span<const float> bands, float elapsedMs) noexcept {
        using namespace config;
        const std::size_t count = std::min<std::size_t>(beatBassBands, bands.size());
        float bass = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            bass += bands[i];
        }
        bass = count > 0 ? bass / static_cast<float>(count) : 0.0f;

        m_sinceBeatMs += elapsedMs;
        const bool beat =
            bass >= beatMinLevel && bass - m_average >= beatRise && m_sinceBeatMs >= static_cast<float>(beatMinGapMs);
        if (beat) {
            m_sinceBeatMs = 0.0f;
            m_pulse = 1.0f;
        } else {
            m_pulse *= std::exp(-elapsedMs / beatDecayMs);
            if (m_pulse < 0.01f) {
                m_pulse = 0.0f;
            }
        }
        const float blend = std::clamp(elapsedMs / beatAverageMs, 0.0f, 1.0f);
        m_average += (bass - m_average) * blend;
        return m_pulse;
    }

    void reset() noexcept { *this = BeatDetector{}; }

private:
    float m_average{};
    float m_pulse{};
    float m_sinceBeatMs{1e6f};
};

}  // namespace threnody::dsp
