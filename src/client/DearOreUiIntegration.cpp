#include "client/DearOreUiIntegration.h"

#ifdef _WIN32
#include <Windows.h>
#include "bridge/DearOreUIBridge.h"
#include "api/IDearOreUIApi.h"
#include "api/manifest/ModManifest.h"
#include "api/manifest/UiManifest.h"
#include "api/types/Page.h"
#endif

namespace vc::client {

DearOreUiIntegration::~DearOreUiIntegration() { shutdown(); }

bool DearOreUiIntegration::initialize(std::wstring const& dllPath) {
#ifdef _WIN32
    shutdown();
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) return false;
    auto query = reinterpret_cast<decltype(&DearOreUI_QueryApi)>(GetProcAddress(module, "DearOreUI_QueryApi"));
    if (!query) { FreeLibrary(module); return false; }
    auto result = query(1);
    if (result.status != DearOreUIBridge_Ok || !result.api) { FreeLibrary(module); return false; }
    auto& api = *static_cast<dearoreui::api::IDearOreUIApi*>(result.api);
    dearoreui::api::ModManifest mod;
    mod.id = dearoreui::api::ModId{"voicechat"};
    mod.modNamespace = "voicechat";
    mod.displayName = "Voice Chat";
    mod.modVersion = dearoreui::api::Version{0, 1, 0};
    mod.permissions = {dearoreui::api::Permission::UiMount, dearoreui::api::Permission::PageObserve};
    auto registered = api.registerMod(mod);
    if (registered.isErr()) { FreeLibrary(module); return false; }
    dearoreui::api::UiManifest page;
    page.modNamespace = "voicechat";
    page.id = "settings";
    page.kind = dearoreui::api::UiKind::Panel;
    page.pageScopes = {dearoreui::api::PageScope::Settings};
    page.anchor = dearoreui::api::UiAnchor::FullScreen;
    page.pointerEvents = true;
    page.fingerprint = "voicechat.settings.1";
    auto ui = api.registerPanel(mod.id, page, "<section id=\"voicechat-settings\"><h2>Voice Chat</h2><p>Voice chat settings are available.</p></section>");
    if (ui.isErr()) { static_cast<void>(api.unregisterMod(mod.id)); FreeLibrary(module); return false; }
    module_ = module;
    api_ = result.api;
    protocolVersion_ = result.protocolVersion;
    modId_ = mod.id;
    uiHandle_ = ui.value();
    modRegistered_ = true;
    return true;
#else
    (void)dllPath;
    return false;
#endif
}

void DearOreUiIntegration::shutdown() {
#ifdef _WIN32
    if (api_ && modRegistered_) {
        auto& api = *static_cast<dearoreui::api::IDearOreUIApi*>(api_);
        static_cast<void>(api.unregisterUi(uiHandle_));
        static_cast<void>(api.unregisterMod(modId_));
    }
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
    modRegistered_ = false;
    uiHandle_ = {};
    modId_ = dearoreui::api::ModId{"voicechat"};
#endif
    module_ = nullptr;
    api_ = nullptr;
    protocolVersion_ = 0;
}

} // namespace vc::client
