#pragma once

#include <cstdint>
#include <string>

namespace vc::client::audio {

struct WasapiProbeResult {
    bool captureAvailable = false;
    bool renderAvailable = false;
    int32_t captureError = 0;
    int32_t renderError = 0;
    std::string detail;
};

// Thin, honest boundary around the Windows audio device API. It only probes
// devices today; PCM streaming is deliberately not claimed until the client
// thread/lifetime contract is confirmed against the game SDK.
class WasapiAudioDevice final {
public:
    static WasapiProbeResult probeDefaultDevices();
};

} // namespace vc::client::audio
