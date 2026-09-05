#pragma once

#include "Config.h"

#include <kiss_fftr.h>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace threnody::dsp {

// Turns a window of mono samples into `spectrumBarCount` bar heights in
// [0, 1]: Hann window, real FFT, log-spaced bands between the configured
// frequencies, dB scale between floor and ceiling, then peak-meter smoothing
// (fast attack, slow release). All buffers are allocated in the constructor.
class SpectrumAnalyzer {
public:
    static constexpr int fftSize = 4096;
    static constexpr int bandCount = config::spectrumBarCount;

    explicit SpectrumAnalyzer(int sampleRate);

    // `samples` must hold exactly fftSize values.
    void analyze(std::span<const float, fftSize> samples) noexcept;

    // One frame without signal: bars fall toward the baseline.
    void decay() noexcept;

    [[nodiscard]] const std::array<float, bandCount>& bands() const noexcept { return m_bands; }
    [[nodiscard]] bool idle() const noexcept;

private:
    struct CfgDeleter {
        void operator()(kiss_fftr_state* cfg) const noexcept { kiss_fftr_free(cfg); }
    };

    void smoothToward(const std::array<float, bandCount>& target) noexcept;

    std::unique_ptr<kiss_fftr_state, CfgDeleter> m_cfg;
    std::vector<float> m_window;
    std::vector<float> m_input;
    std::vector<kiss_fft_cpx> m_output;
    std::array<std::pair<int, int>, bandCount> m_binRanges{};  // [first, last] inclusive
    std::array<float, bandCount> m_bandGainDb{};
    std::array<float, bandCount> m_bands{};
    float m_amplitudeScale{1.0f};
};

}  // namespace threnody::dsp
