#pragma once

#include "Config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace threnody::dsp {

// The waveform for the Wave visualiser: the last `windowSamples` of audio,
// started at a rising zero crossing so a steady tone stands still instead of
// sliding, averaged down to `points` values (which also smooths off the
// hiss), and scaled by a slowly falling peak so quiet and loud songs fill the
// same height.
class Oscilloscope {
public:
    static constexpr int points = config::waveformPoints;
    static constexpr std::size_t windowSamples = 1024;

    // `samples` ends with the newest audio; needs twice the window to find
    // a crossing, and with less just decays.
    void update(std::span<const float> samples) noexcept {
        if (samples.size() < 2 * windowSamples) {
            decay();
            return;
        }
        const std::size_t newest = samples.size() - windowSamples;
        std::size_t start = newest;
        for (std::size_t i = newest; i > newest - windowSamples; --i) {
            if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
                start = i;
                break;
            }
        }

        constexpr std::size_t bucket = windowSamples / points;
        std::array<float, points> shape{};
        float peak = 0.0f;
        for (std::size_t p = 0; p < static_cast<std::size_t>(points); ++p) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < bucket; ++k) {
                sum += samples[start + p * bucket + k];
            }
            shape[p] = sum / static_cast<float>(bucket);
            peak = std::max(peak, std::abs(shape[p]));
        }
        m_peak = std::max({peak, m_peak * config::waveformPeakFall, config::waveformMinPeak});
        for (std::size_t p = 0; p < static_cast<std::size_t>(points); ++p) {
            const float target = std::clamp(shape[p] / m_peak, -1.0f, 1.0f);
            m_shape[p] += (target - m_shape[p]) * config::waveformSmoothing;
        }
    }

    // One frame without signal: the line flattens.
    void decay() noexcept {
        for (float& value : m_shape) {
            value *= config::waveformDecay;
        }
    }

    // In [-1, 1], oldest sample first.
    [[nodiscard]] const std::array<float, points>& shape() const noexcept { return m_shape; }

private:
    std::array<float, points> m_shape{};
    float m_peak{config::waveformMinPeak};
};

}  // namespace threnody::dsp
