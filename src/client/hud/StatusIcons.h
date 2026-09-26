#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"

#include "client/hud/StatusOverlay.h"

namespace ll::event::inline render {
class AfterUIRenderEvent;
}

namespace vc::client::hud {

// 状态图标贴图缓存：把 AudioStatus 映射到资源包里的贴图。
//
// 资源包前提：贴图位于本模组资源包的 textures/ui/voicechat/status_*.png，
// 玩家必须在「设置 -> 全局资源」里启用本模组的资源包，引擎才会解析到这些贴图。
// 未启用时 getTexture 返回空 TexturePtr，本类 ready() 为 false，HUD 自动回落为纯文字状态。
//
// 线程约束：贴图只能经渲染回调里的 MinecraftUIRenderContext 获取（UI 上下文非线程安全），
// 因此 ensureLoaded 只能在 AfterUIRenderEvent 回调内调用；加载成功后缓存 TexturePtr，后续帧只读缓存。
class StatusIcons final {
public:
    // 幂等：首次调用真正加载；若尚未全部就绪，会按冷却时间重试，
    // 这样玩家在游戏内启用资源包后无需重进世界也能生效。
    void ensureLoaded(ll::event::render::AfterUIRenderEvent& event);

    // 释放缓存；下次渲染会重新加载（例如资源包被重载后）。
    void reset();

    // 该状态的贴图；未加载或缺失时返回 nullptr（调用方据此回落为纯文字）。
    mce::ClientTexture const* textureFor(AudioStatus status) const;

    // 全部贴图是否都已加载成功。
    bool ready() const { return ready_; }

    // 是否已至少尝试过一次加载（供调用方做一次性告警，避免逐帧刷屏）。
    bool loadAttempted() const { return attempted_; }

private:
    static constexpr std::size_t kStatusCount = 5;
    static std::size_t           indexOf(AudioStatus status);

    std::array<mce::TexturePtr, kStatusCount> textures_{};
    bool                                      attempted_     = false;
    bool                                      ready_         = false;
    int64_t                                   lastAttemptMs_ = 0;
};

} // namespace vc::client::hud
