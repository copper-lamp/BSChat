#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"

#include "client/hud/StatusOverlay.h"

namespace vc::client::hud {

// 状态图标贴图缓存：把 AudioStatus 映射到本模组自带的 PNG 图标。
//
// 贴图不走资源包：模组目录不在客户端资源包扫描范围内，玩家无法在「设置 -> 全局资源」里启用它，
// 因此改为运行时自举，链路为：
//   1. AppPlatform::loadTexture 从 <模组目录>/icons/status_*.png 解码出 mce::Image；
//   2. ClientInstance::getTextureGroup()->uploadTexture 把像素直接上传进引擎纹理组；
//   3. 缓存得到的 mce::TexturePtr 供绘制使用（不再回读资源包，避免拿到「缺失贴图」占位）。
// 任一张解码或上传失败时 ready() 为 false，HUD 自动回落为纯文字状态。
//
// 线程约束：文件解码与纹理上传都必须发生在渲染回调内（纹理组非线程安全），
// 因此 ensureLoaded 只能在 AfterUIRenderEvent 回调里调用；成功后只读缓存。
class StatusIcons final {
public:
    // 图标所在目录（模组目录下的 icons/）。未设置时 ensureLoaded 直接失败。
    void setIconDirectory(std::filesystem::path directory);

    // 幂等：首次调用真正加载；未就绪时按冷却时间重试，便于补齐图标文件后自动生效。
    void ensureLoaded();

    // 释放缓存；下次渲染会重新加载。
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
    std::filesystem::path                     iconDirectory_{};
    bool                                      attempted_     = false;
    bool                                      ready_         = false;
    int64_t                                   lastAttemptMs_ = 0;
};

} // namespace vc::client::hud
