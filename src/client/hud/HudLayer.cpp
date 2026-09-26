#include "client/hud/HudLayer.h"

#include <chrono>

#include "ll/api/event/EventBus.h"
#include "ll/api/i18n/I18n.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"
#include "shared/util/FileLog.h"

#include "mc/client/game/ClientInstance.h"

#include "client/entry/ClientEventIds.h"

namespace vc::client::hud {
namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void logInfo(std::string const& message) {
    shared::FileLog::info("[hud] " + message);
    if (auto self = ll::mod::NativeMod::current()) self->getLogger().info("[hud] {}", message);
}

void logWarn(std::string const& message) {
    shared::FileLog::warn("[hud] " + message);
    if (auto self = ll::mod::NativeMod::current()) self->getLogger().warn("[hud] {}", message);
}

} // namespace

HudLayer::~HudLayer() { shutdown(); }

bool HudLayer::initialize() {
    auto self = ll::mod::NativeMod::current();
    auto mod  = std::weak_ptr<ll::mod::Mod>(self);
    renderListener_ = ll::event::EventBus::getInstance().emplaceListener<ll::event::render::AfterUIRenderEvent>(
        [this](auto& event) { onAfterUIRender(event); },
        ll::event::EventPriority::Normal,
        mod
    );
    if (!renderListener_) {
        logWarn("failed to register AfterUIRenderEvent listener; HUD stays hidden");
        return false;
    }
    logInfo("HUD layer initialized");
    return true;
}

void HudLayer::shutdown() {
    if (renderListener_) {
        ll::event::EventBus::getInstance().removeListener<ll::event::render::AfterUIRenderEvent>(renderListener_);
        renderListener_.reset();
    }
    // 贴图缓存的纹理对象由 UI 上下文/纹理组持有，退出时主动释放，避免持有到下一局。
    statusIcons_.reset();
    subtitles_.clear();
}

void HudLayer::applyConfig(config::ClientConfig const& config) {
    enabled_ = config.hudEnabled;
    SubtitleOverlay::Options options;
    options.maxLines = config.maxSubtitleLines;
    options.fadeMs = config.subtitleFadeMs;
    options.enabled = config.subtitleEnabled;
    subtitles_.setOptions(options);
}

void HudLayer::pushSttText(protocol::SttTextMessage const& text, int64_t timestampMs) {
    SubtitleLine line;
    line.speaker = text.speakerId;
    line.text = text.text;
    line.isFinal = text.isFinal;
    subtitles_.push(std::move(line), timestampMs);
}

void HudLayer::clearSubtitles() { subtitles_.clear(); }

void HudLayer::setStatusInputs(StatusInputs inputs) { status_.update(inputs); }

std::string HudLayer::statusText(AudioStatus status) const {
    auto const key = StatusOverlay::i18nKey(status);
    auto const translated = ll::i18n::getInstance().get(key, ll::i18n::getDefaultLocaleCode());
    // 未配置语言文件时回落到键名本身，便于在屏幕上直接看出漏配的键。
    return translated.empty() ? std::string(key) : std::string(translated);
}

void HudLayer::onAfterUIRender(ll::event::render::AfterUIRenderEvent& event) {
    if (!enabled_) return;

    auto client = ll::service::getClientInstance();
    if (!client) return;

    if (!screenNameLogged_) {
        // 事件对每个 ScreenView 都会触发；这里记录一次实际屏幕名，便于后续按名字精确过滤。
        screenNameLogged_ = true;
        logInfo("ui render screen name: " + event.screenView().getScreenName());
    }

    // 只在世界内、且没有打开菜单类界面时绘制，避免把 HUD 画到背包/设置等界面上。
    if (!client->isInWorldAndNotShowingAnyMenuScreens()) return;

    // 状态图标贴图只能在渲染回调里解码并上传进纹理组（非线程安全），首次调用时加载并缓存。
    statusIcons_.ensureLoaded();
    if (statusIcons_.loadAttempted()) {
        if (statusIcons_.ready()) {
            if (!iconReadyLogged_) {
                iconReadyLogged_ = true;
                logInfo("status icon textures uploaded from the bundled icons directory");
            }
        } else if (!iconWarningLogged_) {
            iconWarningLogged_ = true;
            logWarn("status icon textures unavailable; falling back to text-only status");
        }
    }

    AudioStatus const status = status_.status();
    HudRenderer::Frame frame;
    frame.subtitles   = subtitles_.visibleLines(nowMs());
    frame.statusText  = statusText(status);
    frame.statusIcon  = statusIcons_.textureFor(status);
    renderer_.draw(event, frame, layout_);
}

} // namespace vc::client::hud
