#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include "api/IDearOreUIApi.h"
#include "api/types/Id.h"
#endif

namespace vc::client {

class DearOreUiIntegration final {
public:
    DearOreUiIntegration() = default;
    ~DearOreUiIntegration();

    DearOreUiIntegration(DearOreUiIntegration const&) = delete;
    DearOreUiIntegration& operator=(DearOreUiIntegration const&) = delete;

    bool initialize(std::wstring const& dllPath);
    void shutdown();
    bool available() const noexcept { return api_ != nullptr; }
    uint32_t protocolVersion() const noexcept { return protocolVersion_; }

private:
    void* module_ = nullptr;
    void* api_ = nullptr;
    uint32_t protocolVersion_ = 0;
#ifdef _WIN32
    dearoreui::api::ModId modId_{"voicechat"};
    dearoreui::api::RegistrationHandle uiHandle_{};
    bool modRegistered_ = false;
#endif
};

} // namespace vc::client
