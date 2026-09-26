#include "client/audio/WasapiAudio.h"

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "core/audio/AudioTypes.h"

#ifdef _WIN32
#include <audioclient.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <windows.h>
#endif

namespace bsc::client::audio {
namespace {
#ifdef _WIN32
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    template <typename T> T** put() { reset(); return reinterpret_cast<T**>(&value_); }
    template <typename T> T* get() const { return reinterpret_cast<T*>(value_); }
    template <typename T> void attach(T* value) noexcept { reset(); value_ = value; }
    void reset() noexcept { if (value_) reinterpret_cast<IUnknown*>(value_)->Release(); value_ = nullptr; }
private:
    void* value_ = nullptr;
};

static float readSample(const BYTE* p, WORD bits, bool isFloat) {
    if (isFloat && bits == 32) return std::clamp(*reinterpret_cast<const float*>(p), -1.0f, 1.0f);
    if (bits == 16) return static_cast<float>(*reinterpret_cast<const int16_t*>(p)) / 32768.0f;
    if (bits == 32) return static_cast<float>(*reinterpret_cast<const int32_t*>(p)) / 2147483648.0f;
    return 0.0f;
}
static void writeSample(BYTE* p, WORD bits, bool isFloat, float value) {
    value = std::clamp(value, -1.0f, 1.0f);
    if (isFloat && bits == 32) { *reinterpret_cast<float*>(p) = value; return; }
    if (bits == 16) { *reinterpret_cast<int16_t*>(p) = static_cast<int16_t>(value * 32767.0f); return; }
    if (bits == 32) *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(value * 2147483647.0f);
}
#endif
}

struct WasapiAudioDevice::Impl {
    explicit Impl(WasapiAudioConfig c) : config(c) {}
    WasapiAudioConfig config;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<WasapiPcmFrame> captureQueue;
    std::deque<WasapiPcmFrame> renderQueue;
    CaptureCallback callback;
    std::thread worker;
    bool running = false;
    bool stopping = false;
    // 采集与渲染各自独立：没有麦克风的玩家也必须能听到别人，反之亦然。
    bool captureActive = false;
    bool renderActive = false;
    uint64_t renderFramesWritten = 0; // 真正写入渲染设备的帧数
    std::string lastError; // 降级/失败的具体步骤与 HRESULT，供外层诊断日志使用
#ifdef _WIN32
    ComPtr captureClient;
    ComPtr renderClient;
    ComPtr captureAudio;
    ComPtr renderAudio;
    UINT32 renderFrames = 0;
#endif

    static std::string describeError(const char* step, HRESULT hr) {
        char buffer[192];
        std::snprintf(buffer, sizeof(buffer), "%s failed, HRESULT=0x%08lX", step, static_cast<unsigned long>(hr));
        return buffer;
    }

    void setLastError(std::string message) {
        std::lock_guard lock(mutex);
        lastError = std::move(message);
    }

    void run() noexcept {
#ifdef _WIN32
        HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
            setLastError(describeError("CoInitializeEx", init));
            finish();
            return;
        }
        auto uninit = [&] { if (SUCCEEDED(init)) CoUninitialize(); };
        // 管线统一格式由 core/config 的 audio.* 决定。两端都用它 Initialize，并交给音频引擎
        // （AUTOCONVERTPCM|SRC_DEFAULT_QUALITY）转换到端点实际采样率与声道，因此不需要自己写
        // 重采样/混声道。注意 AUTOCONVERTPCM 必须搭配 SRC_DEFAULT_QUALITY，否则 Initialize
        // 直接以 E_INVALIDARG 失败。
        constexpr DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        WAVEFORMATEX requested{};
        requested.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        requested.nChannels       = static_cast<WORD>(std::max<uint32_t>(1, config.channels));
        requested.nSamplesPerSec  = std::max<uint32_t>(8000, config.sampleRate);
        requested.wBitsPerSample  = 32;
        requested.nBlockAlign     = static_cast<WORD>(requested.nChannels * requested.wBitsPerSample / 8);
        requested.nAvgBytesPerSec = requested.nSamplesPerSec * requested.nBlockAlign;
        requested.cbSize          = 0;
        // 缓冲时长固定取 Opus 允许的最大帧长：帧长由服务端协商（20/40/60ms），缓冲取上界后
        // 任意合法帧长都能整帧写入，写入长度直接跟随到达帧的实际长度。
        const REFERENCE_TIME bufferDuration =
            static_cast<REFERENCE_TIME>(::bsc::audio::kMaxFrameSizeMs) * 10000; // ms → 100ns 单位

        ComPtr enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put<IUnknown>()));
        IMMDeviceEnumerator* e = enumerator.get<IMMDeviceEnumerator>();
        if (FAILED(hr)) {
            setLastError(describeError("CoCreateInstance(MMDeviceEnumerator)", hr));
            finish();
            uninit();
            return;
        }

        // 采集链路（失败只降级采集，不影响渲染）
        bool captureOk = false;
        bool renderOk = false;
        std::string captureError;
        {
            ComPtr device;
            const char* step = "GetDefaultAudioEndpoint(capture, eCommunications)";
            hr = e->GetDefaultAudioEndpoint(eCapture, eCommunications, reinterpret_cast<IMMDevice**>(device.put<IUnknown>()));
            if (FAILED(hr)) {
                // 没有“通信”角色默认设备时退回控制台角色；未插麦克风时两者都会是
                // ERROR_NOT_FOUND(0x80070490)，属于环境问题而非代码问题。
                step = "GetDefaultAudioEndpoint(capture, eConsole)";
                hr = e->GetDefaultAudioEndpoint(eCapture, eConsole, reinterpret_cast<IMMDevice**>(device.put<IUnknown>()));
            }
            IAudioClient* cap = nullptr;
            if (SUCCEEDED(hr)) { step = "Activate(IAudioClient, capture)"; hr = device.get<IMMDevice>()->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(captureAudio.put<IAudioClient>())); }
            cap = captureAudio.get<IAudioClient>();
            if (SUCCEEDED(hr)) { step = "Initialize(capture)"; hr = cap->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, &requested, nullptr); }
            if (SUCCEEDED(hr)) { step = "GetService(IAudioCaptureClient)"; hr = cap->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(captureClient.put<IUnknown>())); }
            if (SUCCEEDED(hr)) { step = "Start(capture)"; hr = cap->Start(); }
            if (SUCCEEDED(hr)) captureOk = true;
            else captureError = describeError(step, hr);
        }

        // 渲染链路（失败只降级渲染，不影响采集）
        std::string renderError;
        {
            ComPtr device;
            const char* step = "GetDefaultAudioEndpoint(render, eConsole)";
            hr = e->GetDefaultAudioEndpoint(eRender, eConsole, reinterpret_cast<IMMDevice**>(device.put<IUnknown>()));
            IAudioClient* ren = nullptr;
            if (SUCCEEDED(hr)) { step = "Activate(IAudioClient, render)"; hr = device.get<IMMDevice>()->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(renderAudio.put<IAudioClient>())); }
            ren = renderAudio.get<IAudioClient>();
            if (SUCCEEDED(hr)) { step = "Initialize(render)"; hr = ren->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, &requested, nullptr); }
            if (SUCCEEDED(hr)) { step = "GetService(IAudioRenderClient)"; hr = ren->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(renderClient.put<IUnknown>())); }
            if (SUCCEEDED(hr)) { step = "GetBufferSize(render)"; hr = ren->GetBufferSize(&renderFrames); }
            if (SUCCEEDED(hr)) { step = "Start(render)"; hr = ren->Start(); }
            if (SUCCEEDED(hr)) renderOk = true;
            else renderError = describeError(step, hr);
        }

        if (!captureOk && !renderOk) {
            setLastError("capture: " + captureError + "; render: " + renderError);
            finish();
            uninit();
            return;
        }
        // 单侧可用时记录降级原因，外层据此提示“能听不能说 / 能说不能听”。
        if (!captureOk) setLastError("capture unavailable: " + captureError);
        else if (!renderOk) setLastError("render unavailable: " + renderError);
        else setLastError({});
        {
            std::lock_guard lock(mutex);
            captureActive = captureOk;
            renderActive = renderOk;
            running = true;
        }

        const bool isFloat = requested.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        const size_t bytesPerSample = requested.wBitsPerSample / 8;
        const WORD channels = requested.nChannels;
        IAudioClient* cap = captureAudio.get<IAudioClient>();
        IAudioClient* ren = renderAudio.get<IAudioClient>();
        while (true) {
            { std::unique_lock lock(mutex); if (stopping) break; }
            if (captureOk) {
                auto* cc = captureClient.get<IAudioCaptureClient>();
                UINT32 packets = 0;
                while (SUCCEEDED(cc->GetNextPacketSize(&packets)) && packets) {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                    WasapiPcmFrame frame;
                    frame.sampleRate = requested.nSamplesPerSec;
                    frame.channels = channels;
                    frame.samples.resize(static_cast<size_t>(frames) * channels);
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                        for (size_t i = 0; i < frame.samples.size(); ++i) {
                            frame.samples[i] = readSample(data + i * bytesPerSample, requested.wBitsPerSample, isFloat);
                        }
                    }
                    CaptureCallback cb;
                    {
                        std::lock_guard lock(mutex);
                        if (captureQueue.size() >= config.captureQueueCapacity) captureQueue.pop_front();
                        captureQueue.push_back(frame);
                        cb = callback;
                    }
                    if (cb) cb(frame);
                    cc->ReleaseBuffer(frames);
                }
            }
            if (renderOk) {
                auto* rc = renderClient.get<IAudioRenderClient>();
                UINT32 padding = 0;
                // 写入长度跟随队列里那一帧的实际长度（帧长由服务端协商），缓冲取 60ms 上界，
                // 因此 20/40/60ms 帧都能整帧写入，不会切帧或灌静音。
                WasapiPcmFrame frame; bool have = false;
                {
                    std::lock_guard lock(mutex);
                    if (!renderQueue.empty()) {
                        frame = std::move(renderQueue.front());
                        renderQueue.pop_front();
                        have = true;
                    }
                }
                if (have) {
                    const UINT32 framesPerWrite = static_cast<UINT32>(
                        std::max<std::size_t>(1, frame.samples.size() / std::max<uint16_t>(1, channels))
                    );
                    const bool roomEnough =
                        SUCCEEDED(ren->GetCurrentPadding(&padding)) && renderFrames - padding >= framesPerWrite;
                    BYTE* out = nullptr;
                    if (roomEnough && SUCCEEDED(rc->GetBuffer(framesPerWrite, &out))) {
                        for (size_t i = 0; i < frame.samples.size(); ++i) {
                            writeSample(out + i * bytesPerSample, requested.wBitsPerSample, isFloat, frame.samples[i]);
                        }
                        rc->ReleaseBuffer(framesPerWrite, 0);
                        std::lock_guard lock(mutex);
                        renderFramesWritten += framesPerWrite;
                    } else {
                        // 空间不足或取缓冲失败：放回队首，下轮再写，避免丢音频
                        std::lock_guard lock(mutex);
                        renderQueue.push_front(std::move(frame));
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (captureOk) cap->Stop();
        if (renderOk) ren->Stop();
        { std::lock_guard lock(mutex); captureActive = false; renderActive = false; }
        finish();
        uninit();
#else
        finish();
#endif
    }
    void finish() noexcept { std::lock_guard lock(mutex); running = false; stopping = false; wake.notify_all(); }
};

WasapiAudioDevice::WasapiAudioDevice() : WasapiAudioDevice(WasapiAudioConfig{}) {}
WasapiAudioDevice::WasapiAudioDevice(WasapiAudioConfig config) : impl_(std::make_unique<Impl>(config)) {}
WasapiAudioDevice::~WasapiAudioDevice() { stop(); }

bool WasapiAudioDevice::start() {
    std::unique_lock lock(impl_->mutex);
    if (impl_->running || impl_->worker.joinable()) return impl_->running;
    impl_->stopping = false;
    impl_->worker = std::thread([p = impl_.get()] { p->run(); });
    impl_->wake.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return impl_->running || !impl_->worker.joinable();
    });
    return impl_->running;
}
void WasapiAudioDevice::stop() noexcept { { std::lock_guard lock(impl_->mutex); if (!impl_->worker.joinable()) return; impl_->stopping = true; } impl_->wake.notify_all(); if (impl_->worker.joinable()) impl_->worker.join(); }
bool WasapiAudioDevice::isRunning() const noexcept { std::lock_guard lock(impl_->mutex); return impl_->running; }
bool WasapiAudioDevice::enqueueRender(WasapiPcmFrame frame) { std::lock_guard lock(impl_->mutex); if (!impl_->running || !impl_->renderActive || impl_->renderQueue.size() >= impl_->config.renderQueueCapacity) return false; impl_->renderQueue.push_back(std::move(frame)); return true; }
bool WasapiAudioDevice::tryDequeueCapture(WasapiPcmFrame& frame) { std::lock_guard lock(impl_->mutex); if (impl_->captureQueue.empty()) return false; frame = std::move(impl_->captureQueue.front()); impl_->captureQueue.pop_front(); return true; }
void WasapiAudioDevice::setCaptureCallback(CaptureCallback callback) { std::lock_guard lock(impl_->mutex); impl_->callback = std::move(callback); }
std::string WasapiAudioDevice::lastError() const { std::lock_guard lock(impl_->mutex); return impl_->lastError; }
bool WasapiAudioDevice::captureActive() const noexcept { std::lock_guard lock(impl_->mutex); return impl_->captureActive; }
bool WasapiAudioDevice::renderActive() const noexcept { std::lock_guard lock(impl_->mutex); return impl_->renderActive; }
uint64_t WasapiAudioDevice::renderFramesWritten() const noexcept { std::lock_guard lock(impl_->mutex); return impl_->renderFramesWritten; }

WasapiProbeResult WasapiAudioDevice::probeDefaultDevices() {
    WasapiProbeResult result;
#ifdef _WIN32
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED); bool uninit = SUCCEEDED(init);
    IMMDeviceEnumerator* e = nullptr; HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    if (SUCCEEDED(hr)) { IMMDevice* d = nullptr; hr = e->GetDefaultAudioEndpoint(eRender, eConsole, &d); result.renderError = static_cast<int32_t>(hr); result.renderAvailable = SUCCEEDED(hr); if (d) d->Release(); d = nullptr; hr = e->GetDefaultAudioEndpoint(eCapture, eCommunications, &d); result.captureError = static_cast<int32_t>(hr); result.captureAvailable = SUCCEEDED(hr); if (d) d->Release(); } else result.captureError = result.renderError = static_cast<int32_t>(hr);
    if (e) e->Release(); if (uninit) CoUninitialize(); result.detail = (result.captureAvailable && result.renderAvailable) ? "Default WASAPI endpoints are discoverable" : "A default WASAPI endpoint is unavailable";
#else
    result.detail = "WASAPI is only available on Windows client builds";
#endif
    return result;
}
} // namespace bsc::client::audio
