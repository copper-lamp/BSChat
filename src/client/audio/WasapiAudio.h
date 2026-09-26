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
    // 管线统一格式，取自 core/config 的 audio.*：两端都用它 Initialize，由音频引擎
    // （AUTOCONVERTPCM|SRC_DEFAULT_QUALITY）转换到端点实际采样率与声道。
    // 帧长不在这里配置：渲染写长度跟随到达帧的实际长度（服务端协商决定）。
    uint32_t sampleRate = 48000;
    uint16_t channels = 1;
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

    // start() 失败时给出具体失败步骤与 HRESULT；单侧降级时给出降级原因；全部正常时为空串。
    std::string lastError() const;

    // 采集/渲染各自独立：没有麦克风的玩家仍能听到别人，反之亦然。
    // start() 返回 true 表示至少一侧可用，具体哪一侧看这两个标志。
    bool captureActive() const noexcept;
    bool renderActive() const noexcept;

    // 已写入渲染设备的总帧数（设备级事实）：与“交给 render sink”区分开，
    // 用于判断无声问题出在本机播放链路还是上游。
    uint64_t renderFramesWritten() const noexcept;

    // Retained for callers that only need endpoint discovery.
    static WasapiProbeResult probeDefaultDevices();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vc::client::audio
