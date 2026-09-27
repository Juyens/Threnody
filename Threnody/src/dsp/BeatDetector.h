#pragma once

#include "Config.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace threnody::dsp {

// Turns the kick range's energy, one reading per visualiser frame, into a
// pulse in [0, 1] that jumps up on a kick and fades out. See the beat*
// constants in Config.h for the rules; the gist is onset detection on the
// rise in dB against an adaptive threshold, so it works the same for a quiet
// acoustic track and a brickwall-mastered one.
class BeatDetector {
public:
    // `kickDb`: energy of the kick range this frame, in dB (very low when
    // there is no signal). `elapsedMs` since the previous frame.
    float update(float kickDb, float elapsedMs) noexcept {
        using namespace config;
        const float rise = m_haveLevel ? std::max(0.0f, kickDb - m_previousDb) : 0.0f;
        m_previousDb = kickDb;
        m_haveLevel = true;

        // Mean and spread of the recent rises, before adding this one.
        float mean = 0.0f;
        float spread = 0.0f;
        if (m_filled > 0) {
            for (std::size_t i = 0; i < m_filled; ++i) {
                mean += m_rises[i];
            }
            mean /= static_cast<float>(m_filled);
            for (std::size_t i = 0; i < m_filled; ++i) {
                spread += (m_rises[i] - mean) * (m_rises[i] - mean);
            }
            spread = std::sqrt(spread / static_cast<float>(m_filled));
        }
        m_rises[m_next] = rise;
        m_next = (m_next + 1) % m_rises.size();
        m_filled = std::min(m_filled + 1, m_rises.size());

        m_sinceBeatMs += elapsedMs;
        const float threshold = std::max(mean + beatSensitivity * spread, beatMinRiseDb);
        const bool beat = rise >= threshold && kickDb >= beatFloorDb &&
                          m_sinceBeatMs >= static_cast<float>(beatMinGapMs) && m_filled >= 4;

        const float decayMs = std::clamp(m_intervalMs * beatDecayPerInterval, beatMinDecayMs, beatDecayMs);
        m_pulse *= std::exp(-elapsedMs / decayMs);
        if (beat) {
            // A steady beat keeps a running interval; a long silence resets it.
            m_intervalMs = m_sinceBeatMs > 2000.0f ? 2000.0f : m_intervalMs * 0.7f + m_sinceBeatMs * 0.3f;
            m_sinceBeatMs = 0.0f;
            const float standout = spread > 0.0f ? std::clamp((rise - threshold) / (2.0f * spread), 0.0f, 1.0f) : 1.0f;
            m_pulse = std::max(m_pulse, beatMinPulse + (1.0f - beatMinPulse) * standout);
        }
        if (m_pulse < 0.01f) {
            m_pulse = 0.0f;
        }
        return m_pulse;
    }

    void reset() noexcept { *this = BeatDetector{}; }

private:
    std::array<float, config::beatHistoryFrames> m_rises{};
    std::size_t m_next{};
    std::size_t m_filled{};
    float m_previousDb{};
    bool m_haveLevel{false};
    float m_pulse{};
    float m_sinceBeatMs{1e6f};
    float m_intervalMs{2000.0f};
};

}  // namespace threnody::dsp
