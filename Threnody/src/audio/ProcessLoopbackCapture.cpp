#include "audio/ProcessLoopbackCapture.h"

#include "util/Result.h"

#include <unknwn.h>
#include <winrt/base.h>

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <array>
#include <span>

namespace threnody::audio {
namespace {

constexpr REFERENCE_TIME bufferDuration = 2'000'000;  // 200 ms in 100 ns units.
constexpr std::size_t mixChunk = 1024;                // Frames mixed to mono per pass.

// Receives the asynchronous activation result and wakes the capture thread.
struct ActivationHandler : winrt::implements<ActivationHandler, IActivateAudioInterfaceCompletionHandler> {
    explicit ActivationHandler(HANDLE done) : done(done) {}

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) noexcept override {
        HRESULT activation = E_FAIL;
        winrt::com_ptr<IUnknown> unknown;
        const HRESULT hr = operation->GetActivateResult(&activation, unknown.put());
        result = FAILED(hr) ? hr : activation;
        if (SUCCEEDED(result) && unknown) {
            client = unknown.try_as<IAudioClient>();
            if (!client) {
                result = E_NOINTERFACE;
            }
        }
        SetEvent(done);
        return S_OK;
    }

    HANDLE done{};
    HRESULT result{E_PENDING};
    winrt::com_ptr<IAudioClient> client;
};

}  // namespace

ProcessLoopbackCapture::~ProcessLoopbackCapture() {
    stop();
}

void ProcessLoopbackCapture::start(DWORD processId) {
    stop();
    m_processId = processId;
    m_stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!m_stopEvent) {
        fail(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent(stop)");
        return;
    }
    {
        std::scoped_lock lock{m_failureMutex};
        m_failure.clear();
    }
    m_status.store(CaptureStatus::Starting, std::memory_order_release);
    m_thread = std::jthread{[this, processId] { run(processId); }};
}

void ProcessLoopbackCapture::stop() {
    if (m_thread.joinable()) {
        SetEvent(m_stopEvent.get());
        m_thread.join();
    }
    m_stopEvent.reset();
    m_status.store(CaptureStatus::Stopped, std::memory_order_release);
}

std::string ProcessLoopbackCapture::failure() const {
    std::scoped_lock lock{m_failureMutex};
    return m_failure;
}

void ProcessLoopbackCapture::fail(HRESULT hr, const char* context) {
    {
        std::scoped_lock lock{m_failureMutex};
        m_failure = Error::fromHResult(hr, context).describe();
    }
    m_status.store(CaptureStatus::Failed, std::memory_order_release);
}

void ProcessLoopbackCapture::run(DWORD processId) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Activation: the virtual process-loopback device, scoped to the target
    // process tree.
    AUDIOCLIENT_ACTIVATION_PARAMS params{
        .ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK,
        .ProcessLoopbackParams =
            {
                .TargetProcessId = processId,
                .ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE,
            },
    };
    PROPVARIANT activationBlob{};
    activationBlob.vt = VT_BLOB;
    activationBlob.blob.cbSize = sizeof(params);
    activationBlob.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    win32::unique_handle activated{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!activated) {
        fail(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent(activation)");
        return;
    }
    auto handler = winrt::make_self<ActivationHandler>(activated.get());
    winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                             &activationBlob, handler.get(), operation.put());
    if (FAILED(hr)) {
        fail(hr, "ActivateAudioInterfaceAsync(process loopback)");
        return;
    }
    {
        const HANDLE waits[] = {m_stopEvent.get(), activated.get()};
        const DWORD signalled = WaitForMultipleObjects(2, waits, FALSE, 5000);
        if (signalled == WAIT_OBJECT_0) {
            m_status.store(CaptureStatus::Stopped, std::memory_order_release);
            return;
        }
        if (signalled != WAIT_OBJECT_0 + 1) {
            fail(HRESULT_FROM_WIN32(ERROR_TIMEOUT), "waiting for audio interface activation");
            return;
        }
    }
    if (FAILED(handler->result)) {
        fail(handler->result, "audio interface activation result");
        return;
    }
    winrt::com_ptr<IAudioClient> client = handler->client;

    // The virtual device exposes no mix format; ask for what we want.
    WAVEFORMATEX format{
        .wFormatTag = WAVE_FORMAT_IEEE_FLOAT,
        .nChannels = 2,
        .nSamplesPerSec = sampleRate,
        .nAvgBytesPerSec = sampleRate * 2 * sizeof(float),
        .nBlockAlign = 2 * sizeof(float),
        .wBitsPerSample = 32,
        .cbSize = 0,
    };
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            bufferDuration, 0, &format, nullptr);
    if (FAILED(hr)) {
        fail(hr, "IAudioClient::Initialize(loopback, 44100 Hz stereo float)");
        return;
    }

    winrt::com_ptr<IAudioCaptureClient> capture;
    hr = client->GetService(__uuidof(IAudioCaptureClient), capture.put_void());
    if (FAILED(hr)) {
        fail(hr, "IAudioClient::GetService(IAudioCaptureClient)");
        return;
    }

    win32::unique_handle samplesReady{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!samplesReady) {
        fail(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent(samples ready)");
        return;
    }
    hr = client->SetEventHandle(samplesReady.get());
    if (FAILED(hr)) {
        fail(hr, "IAudioClient::SetEventHandle");
        return;
    }
    hr = client->Start();
    if (FAILED(hr)) {
        fail(hr, "IAudioClient::Start");
        return;
    }
    m_status.store(CaptureStatus::Running, std::memory_order_release);

    // Capture loop. Real-time constraints: no allocation, no locks, no log.
    std::array<float, mixChunk> mono{};
    const HANDLE waits[] = {m_stopEvent.get(), samplesReady.get()};
    HRESULT loopError = S_OK;
    const char* loopContext = nullptr;
    for (;;) {
        const DWORD signalled = WaitForMultipleObjects(2, waits, FALSE, 1000);
        if (signalled == WAIT_OBJECT_0) {
            break;
        }
        if (signalled == WAIT_FAILED) {
            loopError = HRESULT_FROM_WIN32(GetLastError());
            loopContext = "WaitForMultipleObjects(capture)";
            break;
        }

        UINT32 packetFrames = 0;
        hr = capture->GetNextPacketSize(&packetFrames);
        while (SUCCEEDED(hr) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr;
            const float* stereo = reinterpret_cast<const float*>(data);
            for (UINT32 offset = 0; offset < frames; offset += mixChunk) {
                const std::size_t count = std::min<std::size_t>(mixChunk, frames - offset);
                if (silent) {
                    std::fill_n(mono.begin(), count, 0.0f);
                } else {
                    for (std::size_t i = 0; i < count; ++i) {
                        const std::size_t frame = 2 * (offset + i);
                        mono[i] = 0.5f * (stereo[frame] + stereo[frame + 1]);
                    }
                }
                m_ring.push(std::span<const float>{mono.data(), count});
            }
            hr = capture->ReleaseBuffer(frames);
            if (FAILED(hr)) {
                break;
            }
            hr = capture->GetNextPacketSize(&packetFrames);
        }
        if (FAILED(hr)) {
            loopError = hr;
            loopContext = "IAudioCaptureClient packet loop";
            break;
        }
    }

    client->Stop();
    if (loopContext != nullptr) {
        fail(loopError, loopContext);
    } else {
        m_status.store(CaptureStatus::Stopped, std::memory_order_release);
    }
}

}  // namespace threnody::audio
