#pragma once

#include <memory>

#include "ll/api/event/Listener.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

namespace vc::client {

// UI 通道探针：验证"客户端原生 HUD 绘制"与"客户端表单"两条通道在真机上是否可用。
// 结论落定后该模块应删除或转为正式实现，不承担任何业务逻辑。
//
// 探针键位：
//   F8  切换 HUD 自绘探针（在 ScreenView 渲染后调用 MinecraftUIRenderContext::drawText），
//       同时写入一次 actionbar 文案，用于对比"原生自绘"与"GuiData 通道"两种呈现。
//   F9  通过 ll::form::SimpleForm 给本地玩家发一张表单，并在日志中记录回调是否触发。
class UiChannelProbe final {
public:
    bool initialize();
    void shutdown();

private:
    void onKey(ll::event::input::KeyInputEvent& event);
    void onAfterUIRender(ll::event::render::AfterUIRenderEvent& event);
    void sendProbeForm();
    void writeActionBarProbe();

    bool overlayVisible_ = false;
    bool renderLogged_ = false;
    std::shared_ptr<ll::event::Listener<ll::event::input::KeyInputEvent>>              keyListener_;
    std::shared_ptr<ll::event::Listener<ll::event::render::AfterUIRenderEvent>>        renderListener_;
};

} // namespace vc::client
