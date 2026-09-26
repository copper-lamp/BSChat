#include "client/hud/StatusIcons.h"

#include <chrono>
#include <string>

#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"

namespace vc::client::hud {
namespace {

// 状态到贴图的唯一顺序来源：indexOf / ensureLoaded 都以它为准。
struct StatusIconSpec {
    AudioStatus status;
    char const* name;
};

constexpr StatusIconSpec kIconSpecs[] = {
    {AudioStatus::Idle,     "idle"    },
    {AudioStatus::Speaking, "speaking"},
    {AudioStatus::Muted,    "muted"   },
    {AudioStatus::Playing,  "playing" },
    {AudioStatus::Silent,   "silent"  },
};

static_assert(sizeof(kIconSpecs) / sizeof(kIconSpecs[0]) == 5);

// 资源包内目录（相对资源包根）。资源包需在「设置 -> 全局资源」中启用。
constexpr char kIconPathPrefix[] = "textures/ui/voicechat/status_";

// 加载失败后的重试冷却，避免逐帧向纹理组请求不存在的贴图。
constexpr int64_t kRetryCooldownMs = 2000;

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::size_t StatusIcons::indexOf(AudioStatus status) {
    for (std::size_t i = 0; i < kStatusCount; ++i) {
        if (kIconSpecs[i].status == status) return i;
    }
    return 0;
}

void StatusIcons::ensureLoaded(ll::event::render::AfterUIRenderEvent& event) {
    if (ready_) return;

    int64_t const now = steadyNowMs();
    if (attempted_ && now - lastAttemptMs_ < kRetryCooldownMs) return;
    attempted_     = true;
    lastAttemptMs_ = now;

    auto& context = event.uiRenderContext();

    bool allLoaded = true;
    for (std::size_t i = 0; i < kStatusCount; ++i) {
        std::string const base = std::string(kIconPathPrefix) + kIconSpecs[i].name;
        // ResourceLocation 的路径是否带 .png 后缀在 SDK 头文件里没有权威说明（路径由运行时资源包解析），
        // 这里先试带后缀、再试不带后缀，任一命中即用；真机需验证哪一种才真正生效。
        textures_[i] = context.getTexture(ResourceLocation(Core::PathView(base + ".png")), false);
        if (!textures_[i].mClientTexture) {
            textures_[i] = context.getTexture(ResourceLocation(Core::PathView(base)), false);
        }
        if (!textures_[i].mClientTexture) {
            allLoaded = false;
        }
    }
    ready_ = allLoaded;
}

void StatusIcons::reset() {
    for (auto& texture : textures_) {
        texture = mce::TexturePtr();
    }
    attempted_     = false;
    ready_         = false;
    lastAttemptMs_ = 0;
}

mce::ClientTexture const* StatusIcons::textureFor(AudioStatus status) const {
    auto const& texture = textures_[indexOf(status)];
    // getClientTexture() 会解引用内部指针，空贴图必须先拦下，否则崩。
    if (!texture.mClientTexture) return nullptr;
    return &texture.getClientTexture();
}

} // namespace vc::client::hud
