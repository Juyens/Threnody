#pragma once

#include "audio/SampleRing.h"
#include "util/Win32.h"

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace threnody::audio {

enum class CaptureStatus { Stopped, Starting, Running, Failed };

// WASAPI loopback of one process tree (Spotify's), 44.1 kHz stereo float,
// mixed down to mono into a lock-free ring. Everything WASAPI happens on a
// dedicated MTA thread: activation, initialisation and the capture loop. The
// loop allocates nothing, takes no locks and never logs; failures are stored
// and surfaced through `status()` / `failure()` for the UI thread to report.
class ProcessLoopbackCapture {
public:
    static constexpr std::uint32_t sampleRate = 44100;
    static constexpr std::size_t ringCapacity = 32768;  // ~0.74 s

    ProcessLoopbackCapture() = default;
    ~ProcessLoopbackCapture();

    ProcessLoopbackCapture(const ProcessLoopbackCapture&) = delete;
    ProcessLoopbackCapture& operator=(const ProcessLoopbackCapture&) = delete;

    // Starts capturing `processId` and its children. Returns immediately;
    // watch `status()`.
    void start(DWORD processId);
    void stop();

    [[nodiscard]] CaptureStatus status() const noexcept { return m_status.load(std::memory_order_acquire); }
    [[nodiscard]] std::string failure() const;
    [[nodiscard]] DWORD processId() const noexcept { return m_processId; }
    [[nodiscard]] const SampleRing<ringCapacity>& samples() const noexcept { return m_ring; }

private:
    void run(DWORD processId);
    void fail(HRESULT hr, const char* context);

    std::jthread m_thread;
    win32::unique_handle m_stopEvent;
    DWORD m_processId{};

    std::atomic<CaptureStatus> m_status{CaptureStatus::Stopped};
    mutable std::mutex m_failureMutex;
    std::string m_failure;

    SampleRing<ringCapacity> m_ring;
};

}  // namespace threnody::audio
