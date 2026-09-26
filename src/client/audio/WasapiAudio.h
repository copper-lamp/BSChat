#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vc::client::audio {

struct WasapiProbeResult {
    bool captureAvailable = false;
    bool renderAvailable = false;
    int32_t captureError = 0;
    int32_t renderError = 0;
    std::string detail;
};

// Interleaved, normalized PCM. The queue owns the samples, so a frame may be
// retained by the consumer after the capture callback returns.
struct WasapiPcmFrame {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::vector<float> samples;
};

struct WasapiAudioConfig {
    uint32_t captureQueueCapacity = 32;
    uint32_t renderQueueCapacity = 64;
};

// Thread-safe boundary around the default shared-mode WASAPI endpoints.
// start/stop may be called from any thread (including repeatedly); audio
// callbacks run only on the capture worker and must not call stop().
class WasapiAudioDevice final {
public:
    using CaptureCallback = std::function<void(const WasapiPcmFrame&)>;

    WasapiAudioDevice();
    explicit WasapiAudioDevice(WasapiAudioConfig config);
    ~WasapiAudioDevice();

    WasapiAudioDevice(const WasapiAudioDevice&) = delete;
    WasapiAudioDevice& operator=(const WasapiAudioDevice&) = delete;

    // Opens and starts both default endpoints. Returns false on any failure;
    // a partial start is rolled back. Calling start while running is a no-op.
    bool start();
    void stop() noexcept;
    bool isRunning() const noexcept;

    // Copies a frame into the bounded render queue. False means stopped or
    // full; dropping is explicit and preferable to blocking an audio thread.
    bool enqueueRender(WasapiPcmFrame frame);
    bool tryDequeueCapture(WasapiPcmFrame& frame);
    void setCaptureCallback(CaptureCallback callback);

    // start() 失败时给出具体失败步骤与 HRESULT；成功或未启动时为空串。
    std::string lastError() const;

    // Retained for callers that only need endpoint discovery.
    static WasapiProbeResult probeDefaultDevices();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vc::client::audio
