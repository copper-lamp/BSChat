#include "client/hud/StatusIcons.h"

#include <chrono>
#include <string>
#include <utility>

#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/application/AppPlatform.h"
#include "mc/deps/core/file/Path.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTexture.h"

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

// 上传进纹理组时使用的键。它只作为纹理组内部的缓存键，不参与资源包解析，因此不带 .png 后缀。
constexpr char kTextureKeyPrefix[] = "textures/ui/voicechat/status_";

// 加载失败后的重试冷却，避免逐帧重复解码 PNG。
constexpr int64_t kRetryCooldownMs = 2000;

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void StatusIcons::setIconDirectory(std::filesystem::path directory) {
    if (directory == iconDirectory_) return;
    iconDirectory_ = std::move(directory);
    reset();
}

std::size_t StatusIcons::indexOf(AudioStatus status) {
    for (std::size_t i = 0; i < kStatusCount; ++i) {
        if (kIconSpecs[i].status == status) return i;
    }
    return 0;
}

void StatusIcons::ensureLoaded() {
    if (ready_ || iconDirectory_.empty()) return;

    int64_t const now = steadyNowMs();
    if (attempted_ && now - lastAttemptMs_ < kRetryCooldownMs) return;
    attempted_     = true;
    lastAttemptMs_ = now;

    auto client = ll::service::getClientInstance();
    if (!client) return;
    auto appPlatform = ll::service::getAppPlatform();
    if (!appPlatform) return;
    auto textureGroup = client->getTextureGroup();
    if (!textureGroup) return;

    bool allLoaded = true;
    for (std::size_t i = 0; i < kStatusCount; ++i) {
        // 已成功的贴图不重复上传；逐个跳过可让「部分成功」在补齐文件后增量恢复。
        if (textures_[i].mClientTexture) continue;

        std::filesystem::path const file =
            iconDirectory_ / (std::string("status_") + kIconSpecs[i].name + ".png");
        mce::Image image = appPlatform->loadTexture(Core::Path(file));
        if (image.isEmpty()) {
            allLoaded = false;
            continue;
        }

        std::string const  key = std::string(kTextureKeyPrefix) + kIconSpecs[i].name;
        ResourceLocation   location{Core::PathView(std::string_view(key))};
        BedrockTexture&    uploaded = textureGroup->uploadTexture(location, cg::ImageBuffer(std::move(image)));
        textures_[i]                = mce::TexturePtr(uploaded, location, cg::TextureSetLayerType::Color);
        if (!textures_[i].mClientTexture) allLoaded = false;
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
