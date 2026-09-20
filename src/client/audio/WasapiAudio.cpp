#include "client/audio/WasapiAudio.h"

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <combaseapi.h>
#endif

namespace vc::client::audio {

WasapiProbeResult WasapiAudioDevice::probeDefaultDevices() {
    WasapiProbeResult result;
#ifdef _WIN32
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(init);
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(hr)) {
        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        result.renderError = static_cast<int32_t>(hr);
        result.renderAvailable = SUCCEEDED(hr);
        if (device) device->Release();
        device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device);
        result.captureError = static_cast<int32_t>(hr);
        result.captureAvailable = SUCCEEDED(hr);
        if (device) device->Release();
    } else {
        result.captureError = static_cast<int32_t>(hr);
        result.renderError = static_cast<int32_t>(hr);
    }
    if (enumerator) enumerator->Release();
    if (shouldUninitialize) CoUninitialize();
    result.detail = (result.captureAvailable && result.renderAvailable)
        ? "Default WASAPI endpoints are discoverable"
        : "A default WASAPI endpoint is unavailable";
#else
    result.detail = "WASAPI is only available on Windows client builds";
#endif
    return result;
}

} // namespace vc::client::audio
