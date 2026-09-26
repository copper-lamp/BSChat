#include "client/spike/UiChannelProbe.h"

#include <optional>
#include <string>

#include "client/entry/ClientEventIds.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"
#include "shared/util/FileLog.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"

namespace vc::client {

namespace {

// 探针键位不在配置系统中注册，避免污染正式配置；仅用于本轮通道验证。
constexpr int ProbeOverlayKey = 0x77; // VK_F8
constexpr int ProbeFormKey    = 0x78; // VK_F9

void probeLog(std::string const& message) {
    shared::FileLog::info("[ui-probe] " + message);
    if (auto self = ll::mod::NativeMod::current()) self->getLogger().info("[ui-probe] {}", message);
}

void probeLogError(std::string const& message) {
    shared::FileLog::warn("[ui-probe] " + message);
    if (auto self = ll::mod::NativeMod::current()) self->getLogger().warn("[ui-probe] {}", message);
}

} // namespace

bool UiChannelProbe::initialize() {
    auto self = ll::mod::NativeMod::current();
    auto mod  = std::weak_ptr<ll::mod::Mod>(self);
    auto& bus = ll::event::EventBus::getInstance();

    keyListener_ = bus.emplaceListener<ll::event::input::KeyInputEvent>(
        [this](auto& event) { onKey(event); },
        ll::event::EventPriority::Normal,
        mod
    );
    renderListener_ = bus.emplaceListener<ll::event::render::AfterUIRenderEvent>(
        [this](auto& event) { onAfterUIRender(event); },
        ll::event::EventPriority::Normal,
        mod
    );
    if (!keyListener_ || !renderListener_) {
        probeLogError(
            "listener registration failed: key=" + std::string(keyListener_ ? "ok" : "failed")
            + " afterUIRender=" + std::string(renderListener_ ? "ok" : "failed")
        );
        shutdown();
        return false;
    }
    probeLog("initialized; F8 toggles the HUD draw probe, F9 sends a SimpleForm to the local player");
    return true;
}

void UiChannelProbe::shutdown() {
    auto& bus = ll::event::EventBus::getInstance();
    if (keyListener_) {
        bus.removeListener<ll::event::input::KeyInputEvent>(keyListener_);
        keyListener_.reset();
    }
    if (renderListener_) {
        bus.removeListener<ll::event::render::AfterUIRenderEvent>(renderListener_);
        renderListener_.reset();
    }
    overlayVisible_ = false;
    renderLogged_   = false;
}

void UiChannelProbe::onKey(ll::event::input::KeyInputEvent& event) {
    if (!event.isDown()) return;
    if (event.keyCode() == ProbeOverlayKey) {
        overlayVisible_ = !overlayVisible_;
        if (overlayVisible_) {
            renderLogged_ = false;
            writeActionBarProbe();
        }
        probeLog("HUD draw probe " + std::string(overlayVisible_ ? "enabled" : "disabled"));
    } else if (event.keyCode() == ProbeFormKey) {
        sendProbeForm();
    }
}

void UiChannelProbe::onAfterUIRender(ll::event::render::AfterUIRenderEvent& event) {
    if (!overlayVisible_) return;

    auto client = ll::service::getClientInstance();
    if (!client) {
        if (!renderLogged_) probeLogError("after UIRender fired but ClientInstance is unavailable");
        return;
    }
    FontHandle fontHandle = client->getFontHandle();
    if (!fontHandle.mDefaultFont) {
        if (!renderLogged_) probeLogError("after UIRender fired but the default font handle is empty");
        return;
    }

    // HUD 自绘通道：在 ScreenView 渲染完成后追加文本，随后立即 flush，
    // 因为 origin() 内部已经做过一次提交，此处必须自己收尾。
    auto& context = event.uiRenderContext();
    std::string text = "BSChat UI probe: HUD draw channel alive";
    RectangleArea rect(10.0f, 60.0f, 510.0f, 80.0f, false);
    context.drawText(
        *fontHandle.mDefaultFont,
        rect,
        std::move(text),
        mce::Color(1.0f, 0.85f, 0.2f, 1.0f),
        1.0f,
        ui::TextAlignment::Left,
        TextMeasureData{1.0f, 0.0f, true, false, false, ui::TextAlignment::Left},
        CaretMeasureData{0, false}
    );
    context.flushText(0.0f, std::nullopt);

    if (!renderLogged_) {
        renderLogged_ = true;
        probeLog("HUD draw channel: drawText + flushText executed on the UI render context");
    }
}

void UiChannelProbe::writeActionBarProbe() {
    auto client = ll::service::getClientInstance();
    if (!client) {
        probeLogError("GuiData channel: ClientInstance is unavailable");
        return;
    }
    auto guiData = client->getGuiData();
    guiData->setActionBarMessage("BSChat UI probe: action bar channel alive", std::nullopt);
    probeLog("GuiData channel: setActionBarMessage issued");
}

void UiChannelProbe::sendProbeForm() {
    auto client = ll::service::getClientInstance();
    if (!client) {
        probeLogError("form channel: ClientInstance is unavailable");
        return;
    }
    Player* player = client->getLocalPlayer();
    if (!player) {
        probeLogError("form channel: getLocalPlayer() returned null, no local player context");
        return;
    }
    probeLog("form channel: local player resolved, building the form payload");

    // 本地可见标记：确认 F9 按键确实到达探针，且本地 UI 写入通道可用。
    client->getGuiData()->showPopupNotice("BSChat UI probe", "F9 received, form dispatch attempted");

    // 面板定义 JSON -> 表单 JSON -> 底层发送原语。这条链路就是后续 FormPanelBuilder 的形状，
    // 因此这里用 CustomForm::getFormData() + Form::sendRawTo() 作为决定性取证：
    // sendRawTo 返回 bool，可以直接判定 LeviLamina 是否真的把表单投递出去。
    ll::form::CustomForm form("BSChat UI probe");
    form.appendLabel("client-side form channel probe");
    form.appendToggle("subtitle", "subtitle enabled", true);
    form.appendDropdown("talkMode", "talk mode", {"disabled", "push to talk", "voice activity"});
    form.appendSlider("volume", "playback volume", 0.0, 100.0, 5.0, 80.0);
    auto payload = form.getFormData();
    probeLog("form channel: form payload length=" + std::to_string(payload.size()));

    bool dispatched = ll::form::Form::sendRawTo(
        *player,
        payload,
        [](Player&, std::optional<std::string> response, ll::form::FormCancelReason reason) {
            probeLog(
                "form channel: response callback fired, hasResponse="
                + std::string(response ? "true" : "false") + " cancelReason="
                + (reason ? std::to_string(static_cast<int>(*reason)) : std::string("none"))
            );
        }
    );
    probeLog("form channel: sendRawTo returned " + std::string(dispatched ? "true" : "false"));
}

} // namespace vc::client
