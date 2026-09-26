#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/config/Config.h"
#include "core/protocol/Message.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "client/hud/HudRenderer.h"
#include "client/hud/StatusOverlay.h"
#include "client/hud/SubtitleOverlay.h"

namespace vc::client::hud {

// HUD 装配层：订阅客户端 UI 渲染事件，把字幕与状态两个数据模型交给 HudRenderer 绘制。
// 数据来源由外层（ClientMod / ClientRuntime）推入，本层不主动拉取网络或音频状态。
class HudLayer final {
public:
    HudLayer() = default;
    ~HudLayer();

    HudLayer(HudLayer const&) = delete;
    HudLayer& operator=(HudLayer const&) = delete;

    bool initialize();
    void shutdown();

    // 配置热生效：字幕开关/行数/淡出、覆盖层总开关。
    void applyConfig(config::ClientConfig const& config);

    // 网络线程：收到一条 STT 文本
    void pushSttText(protocol::SttTextMessage const& text, int64_t nowMs);

    void clearSubtitles();
    void setStatusInputs(StatusInputs inputs);

private:
    void onAfterUIRender(ll::event::render::AfterUIRenderEvent& event);
    std::string statusText(AudioStatus status) const;

    std::shared_ptr<ll::event::Listener<ll::event::render::AfterUIRenderEvent>> renderListener_;
    SubtitleOverlay subtitles_;
    StatusOverlay   status_;
    HudRenderer     renderer_;
    HudRenderer::Layout layout_{};
    bool enabled_ = true;
    bool screenNameLogged_ = false;
};

} // namespace vc::client::hud
