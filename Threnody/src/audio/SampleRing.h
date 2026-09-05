#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace threnody::audio {

// Single-producer, single-consumer ring of mono samples. The audio thread
// pushes; the UI thread copies the most recent window. No locks, no
// allocation after construction. A reader that is lapped by the writer gets a
// torn window, which is harmless for a visualiser and never unsafe.
template <std::size_t Capacity>
class SampleRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    static constexpr std::size_t capacity = Capacity;

    void push(std::span<const float> samples) noexcept {
        std::uint64_t written = m_written.load(std::memory_order_relaxed);
        for (const float sample : samples) {
            m_data[static_cast<std::size_t>(written & mask)] = sample;
            ++written;
        }
        m_written.store(written, std::memory_order_release);
    }

    // Fills `out` with the latest out.size() samples (zero-padded at the front
    // if fewer have ever been written). Returns how many were real.
    std::size_t latest(std::span<float> out) const noexcept {
        const std::uint64_t written = m_written.load(std::memory_order_acquire);
        const std::size_t wanted = std::min(out.size(), Capacity);
        const std::size_t available = static_cast<std::size_t>(std::min<std::uint64_t>(written, wanted));

        std::fill(out.begin(), out.end() - static_cast<std::ptrdiff_t>(available), 0.0f);
        std::uint64_t index = written - available;
        for (std::size_t i = out.size() - available; i < out.size(); ++i, ++index) {
            out[i] = m_data[static_cast<std::size_t>(index & mask)];
        }
        return available;
    }

    [[nodiscard]] std::uint64_t totalWritten() const noexcept { return m_written.load(std::memory_order_acquire); }

private:
    static constexpr std::uint64_t mask = Capacity - 1;

    std::array<float, Capacity> m_data{};
    std::atomic<std::uint64_t> m_written{0};
};

}  // namespace threnody::audio
