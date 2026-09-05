#include "dsp/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace threnody::dsp {
namespace {

constexpr float epsilon = 1e-12f;

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(int sampleRate)
    : m_cfg(kiss_fftr_alloc(fftSize, 0, nullptr, nullptr)),
      m_window(static_cast<std::size_t>(fftSize)),
      m_input(static_cast<std::size_t>(fftSize)),
      m_output(static_cast<std::size_t>(fftSize / 2 + 1)) {
    // Hann window. Its coherent gain is 0.5, folded into the amplitude scale
    // so a full-scale sine reads 0 dB.
    for (int i = 0; i < fftSize; ++i) {
        m_window[static_cast<std::size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(fftSize)));
    }
    m_amplitudeScale = 2.0f / (0.5f * static_cast<float>(fftSize));

    // Log-spaced band edges; each band covers at least one bin.
    const double binHz = static_cast<double>(sampleRate) / fftSize;
    const double ratio = config::spectrumMaxHz / config::spectrumMinHz;
    int previousLast = 0;
    for (int band = 0; band < bandCount; ++band) {
        const double lowHz = config::spectrumMinHz * std::pow(ratio, static_cast<double>(band) / bandCount);
        const double highHz = config::spectrumMinHz * std::pow(ratio, static_cast<double>(band + 1) / bandCount);
        int first = std::max(1, static_cast<int>(std::floor(lowHz / binHz)));
        int last = std::max(first, static_cast<int>(std::ceil(highHz / binHz)) - 1);
        first = std::max(first, previousLast + 1);
        last = std::clamp(std::max(last, first), first, fftSize / 2);
        m_binRanges[static_cast<std::size_t>(band)] = {first, last};
        previousLast = last;

        const double centreHz = std::sqrt(lowHz * highHz);
        const double octaves = std::log2(centreHz / config::spectrumMinHz);
        m_bandGainDb[static_cast<std::size_t>(band)] = static_cast<float>(octaves) * config::spectrumTiltDbPerOctave;
    }
}

void SpectrumAnalyzer::analyze(std::span<const float, fftSize> samples) noexcept {
    for (int i = 0; i < fftSize; ++i) {
        const auto index = static_cast<std::size_t>(i);
        m_input[index] = samples[index] * m_window[index];
    }
    kiss_fftr(m_cfg.get(), m_input.data(), m_output.data());

    std::array<float, bandCount> target{};
    for (int band = 0; band < bandCount; ++band) {
        const auto [first, last] = m_binRanges[static_cast<std::size_t>(band)];
        float power = 0.0f;
        for (int bin = first; bin <= last; ++bin) {
            const kiss_fft_cpx& c = m_output[static_cast<std::size_t>(bin)];
            power += c.r * c.r + c.i * c.i;
        }
        power /= static_cast<float>(last - first + 1);
        const float amplitude = std::sqrt(power) * m_amplitudeScale;
        const float db = 20.0f * std::log10(amplitude + epsilon) + m_bandGainDb[static_cast<std::size_t>(band)];
        target[static_cast<std::size_t>(band)] =
            std::clamp((db - config::spectrumFloorDb) / (config::spectrumCeilingDb - config::spectrumFloorDb), 0.0f, 1.0f);
    }
    smoothToward(target);
}

void SpectrumAnalyzer::decay() noexcept {
    smoothToward(std::array<float, bandCount>{});
}

void SpectrumAnalyzer::smoothToward(const std::array<float, bandCount>& target) noexcept {
    for (std::size_t i = 0; i < m_bands.size(); ++i) {
        const float current = m_bands[i];
        const float next = target[i];
        if (next > current) {
            m_bands[i] = current + (next - current) * config::spectrumAttack;
        } else {
            m_bands[i] = current * config::spectrumRelease + next * (1.0f - config::spectrumRelease);
        }
        if (m_bands[i] < 0.002f) {
            m_bands[i] = 0.0f;
        }
    }
}

bool SpectrumAnalyzer::idle() const noexcept {
    return std::all_of(m_bands.begin(), m_bands.end(), [](float v) { return v == 0.0f; });
}

}  // namespace threnody::dsp
