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

#ifdef _WIN32
#include <audioclient.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <windows.h>
#endif

namespace vc::client::audio {
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
    std::string lastError; // 启动失败的具体步骤与 HRESULT，供外层诊断日志使用
#ifdef _WIN32
    ComPtr captureClient;
    ComPtr renderClient;
    ComPtr captureAudio;
    ComPtr renderAudio;
    WAVEFORMATEX* format = nullptr;
    UINT32 renderFrames = 0;
#endif

    void setError(const char* step, HRESULT hr) {
        char buffer[192];
        std::snprintf(buffer, sizeof(buffer), "%s failed, HRESULT=0x%08lX", step, static_cast<unsigned long>(hr));
        std::lock_guard lock(mutex);
        lastError = buffer;
    }

    void run() noexcept {
#ifdef _WIN32
        HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) { setError("CoInitializeEx", init); finish(); return; }
        auto uninit = [&] { if (SUCCEEDED(init)) CoUninitialize(); };
        // AUTOCONVERTPCM 必须与 SRC_DEFAULT_QUALITY 同时使用，否则 Initialize 会以
        // E_INVALIDARG 失败（Windows 音频引擎的硬性要求）。
        constexpr DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        ComPtr enumerator, captureDevice, renderDevice;
        const char* step = "CoCreateInstance(MMDeviceEnumerator)";
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put<IUnknown>()));
        IMMDeviceEnumerator* e = enumerator.get<IMMDeviceEnumerator>();
        if (SUCCEEDED(hr)) {
            step = "GetDefaultAudioEndpoint(capture, eCommunications)";
            hr = e->GetDefaultAudioEndpoint(eCapture, eCommunications, reinterpret_cast<IMMDevice**>(captureDevice.put<IUnknown>()));
            if (FAILED(hr)) {
                // 例如设备没有配置“通信”角色的默认麦克风时退回控制台角色。
                step = "GetDefaultAudioEndpoint(capture, eConsole)";
                hr = e->GetDefaultAudioEndpoint(eCapture, eConsole, reinterpret_cast<IMMDevice**>(captureDevice.put<IUnknown>()));
            }
        }
        if (SUCCEEDED(hr)) { step = "GetDefaultAudioEndpoint(render, eConsole)"; hr = e->GetDefaultAudioEndpoint(eRender, eConsole, reinterpret_cast<IMMDevice**>(renderDevice.put<IUnknown>())); }
        if (SUCCEEDED(hr)) { step = "Activate(IAudioClient, capture)"; hr = captureDevice.get<IMMDevice>()->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(captureAudio.put<IAudioClient>())); }
        if (SUCCEEDED(hr)) { step = "Activate(IAudioClient, render)"; hr = renderDevice.get<IMMDevice>()->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(renderAudio.put<IAudioClient>())); }
        IAudioClient* cap = captureAudio.get<IAudioClient>();
        IAudioClient* ren = renderAudio.get<IAudioClient>();
        if (SUCCEEDED(hr)) { step = "GetMixFormat(capture)"; hr = cap->GetMixFormat(&format); }
        if (SUCCEEDED(hr)) { step = "Initialize(capture)"; hr = cap->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 10000000, 0, format, nullptr); }
        if (SUCCEEDED(hr)) { step = "Initialize(render)"; hr = ren->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 10000000, 0, format, nullptr); }
        if (SUCCEEDED(hr)) { step = "GetService(IAudioCaptureClient)"; hr = cap->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(captureClient.put<IUnknown>())); }
        if (SUCCEEDED(hr)) { step = "GetService(IAudioRenderClient)"; hr = ren->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(renderClient.put<IUnknown>())); }
        if (SUCCEEDED(hr)) { step = "GetBufferSize(render)"; hr = ren->GetBufferSize(&renderFrames); }
        if (SUCCEEDED(hr)) { step = "Start(capture)"; hr = cap->Start(); }
        if (SUCCEEDED(hr)) { step = "Start(render)"; hr = ren->Start(); }
        if (FAILED(hr)) {
            setError(step, hr);
            if (format) { CoTaskMemFree(format); format = nullptr; }
            finish();
            uninit();
            return;
        }
        { std::lock_guard lock(mutex); running = true; }
        // ComPtr cannot adopt stack pointers, so retain clients through local raw
        // references and release only after the loop; service objects retain them.
        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        const size_t bytesPerSample = format->wBitsPerSample / 8;
        while (true) {
            { std::unique_lock lock(mutex); if (stopping) break; }
            auto* cc = captureClient.get<IAudioCaptureClient>();
            UINT32 packets = 0; if (FAILED(cc->GetNextPacketSize(&packets))) break;
            while (packets) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                WasapiPcmFrame frame; frame.sampleRate = format->nSamplesPerSec; frame.channels = format->nChannels;
                frame.samples.resize(static_cast<size_t>(frames) * format->nChannels);
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) for (size_t i = 0; i < frame.samples.size(); ++i) frame.samples[i] = readSample(data + i * bytesPerSample, format->wBitsPerSample, isFloat);
                CaptureCallback cb; { std::lock_guard lock(mutex); if (captureQueue.size() >= config.captureQueueCapacity) captureQueue.pop_front(); captureQueue.push_back(frame); cb = callback; }
                if (cb) cb(frame);
                cc->ReleaseBuffer(frames); if (FAILED(cc->GetNextPacketSize(&packets))) packets = 0;
            }
            auto* rc = renderClient.get<IAudioRenderClient>(); UINT32 padding = 0;
            if (SUCCEEDED(ren->GetCurrentPadding(&padding)) && renderFrames > padding) {
                UINT32 available = renderFrames - padding; BYTE* out = nullptr;
                if (SUCCEEDED(rc->GetBuffer(available, &out))) {
                    WasapiPcmFrame frame; bool have = false; { std::lock_guard lock(mutex); if (!renderQueue.empty()) { frame = std::move(renderQueue.front()); renderQueue.pop_front(); have = true; } }
                    const size_t total = static_cast<size_t>(available) * format->nChannels;
                    for (size_t i = 0; i < total; ++i) { float sample = have && i < frame.samples.size() ? frame.samples[i] : 0.0f; writeSample(out + i * bytesPerSample, format->wBitsPerSample, isFloat, sample); }
                    rc->ReleaseBuffer(available, 0);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        cap->Stop(); ren->Stop(); if (format) { CoTaskMemFree(format); format = nullptr; } finish(); uninit();
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
bool WasapiAudioDevice::enqueueRender(WasapiPcmFrame frame) { std::lock_guard lock(impl_->mutex); if (!impl_->running || impl_->renderQueue.size() >= impl_->config.renderQueueCapacity) return false; impl_->renderQueue.push_back(std::move(frame)); return true; }
bool WasapiAudioDevice::tryDequeueCapture(WasapiPcmFrame& frame) { std::lock_guard lock(impl_->mutex); if (impl_->captureQueue.empty()) return false; frame = std::move(impl_->captureQueue.front()); impl_->captureQueue.pop_front(); return true; }
void WasapiAudioDevice::setCaptureCallback(CaptureCallback callback) { std::lock_guard lock(impl_->mutex); impl_->callback = std::move(callback); }
std::string WasapiAudioDevice::lastError() const { std::lock_guard lock(impl_->mutex); return impl_->lastError; }

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
} // namespace vc::client::audio
