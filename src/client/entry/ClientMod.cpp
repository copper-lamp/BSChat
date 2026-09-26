#include "client/entry/ClientMod.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "client/entry/ClientEventIds.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/world/actor/player/Player.h"
#include "shared/transport/GamePacketTransport.h"
#include "shared/util/FileLog.h"
#include "shared/util/PlayerIdUtils.h"

namespace vc::client {

void PlayerState::set(Player* player) { player_ = player; }
protocol::PlayerId PlayerState::playerId() const {
    return player_ ? shared::playerIdFromUuid(player_->getUuid()) : protocol::PlayerId{};
}
Position PlayerState::position() const {
    if (!player_) return {};
    auto const& pos = player_->getPosition();
    Position result;
    result.x = pos.x;
    result.y = pos.y;
    result.z = pos.z;
    result.dimensionId = static_cast<int32_t>(player_->getDimensionId());
    return result;
}

namespace {

class SteadyClock final : public IClock {
public:
    int64_t nowMs() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << text;
    return output.good();
}

} // namespace

bool ClientMod::load() {
    auto self = ll::mod::NativeMod::current();
    if (!self) return false;

    // 纯文本日志与宿主日志并行输出，方便在没有控制台的场景下取证。
    logPath_ = self->getConfigDir() / "voicechat-client.log";
    shared::FileLog::reset(logPath_.string(), "voicechat client log started");
    shared::FileLog::info("load: client mod loading, config dir = " + self->getConfigDir().string());

    auto path = self->getConfigDir() / "voicechat.json";
    auto text = readText(path);
    config::ClientConfig config{};
    if (!text.empty()) config = config::clientConfigFromJson(text);
    else {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (!writeText(path, config::clientConfigToJson(config))) return false;
    }

    transport_ = std::make_unique<shared::GamePacketTransport>(shared::TransportMode::Client);
    auto dearOreUiPath = self->getConfigDir().parent_path().parent_path() / "mods" / "DearOreUI" / "DearOreUI.dll";
    dearOreUiPath_ = dearOreUiPath.wstring();
    playerState_ = std::make_unique<PlayerState>();
    clock_ = std::make_unique<SteadyClock>();
    config_ = std::move(config);
    // 音频管线格式由配置决定，WASAPI 两端都按它 Initialize，由引擎转换到端点实际格式。
    audio::WasapiAudioConfig audioConfig;
    audioConfig.sampleRate = static_cast<uint32_t>(std::max(8000, config_.audio.sampleRate));
    audioConfig.channels = static_cast<uint16_t>(std::max(1, config_.audio.channels));
    audioConfig.frameSamples = static_cast<uint32_t>(std::max<int64_t>(
        1,
        static_cast<int64_t>(audioConfig.sampleRate) * std::max(1, config_.audio.frameSizeMs) / 1000
    ));
    audioDevice_ = std::make_unique<audio::WasapiAudioDevice>(audioConfig);
    shared::FileLog::info("load: complete");
    return true;
}

bool ClientMod::enable() {
    auto self = ll::mod::NativeMod::current();
    if (!transport_ || !playerState_ || !clock_) {
        if (self) self->getLogger().error("voicechat client enable prerequisites are not ready");
        shared::FileLog::error("enable: prerequisites are not ready (transport/playerState/clock)");
        return false;
    }
    shared::FileLog::info("enable: registering client event listeners");
    if (!dearOreUiPath_.empty() && dearOreUi_.initialize(dearOreUiPath_)) {
        if (self) self->getLogger().info("DearOreUI Settings integration initialized during enable");
        shared::FileLog::info("enable: DearOreUI Settings integration initialized");
    } else {
        if (self) self->getLogger().info("DearOreUI Settings integration unavailable; continuing without optional UI");
        shared::FileLog::info("enable: DearOreUI Settings integration unavailable; continuing without optional UI");
    }
    // 事件条目由 LeviLamina.dll 的 hook 型 emitter 在加载期注册。
    // 本模组不注册 emitter，只注册监听器；事件 ID 见文件头部的 getEventId 绑定。
    auto& bus = ll::event::EventBus::getInstance();
    auto mod = std::weak_ptr<ll::mod::Mod>(self);
    joinListener_ = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>([this](auto& event) { onJoin(event); }, ll::event::EventPriority::Normal, mod);
    if (!joinListener_) self->getLogger().error("failed to register ClientJoinLevelEvent listener");
    exitListener_ = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>([this](auto& event) { onExit(event); }, ll::event::EventPriority::Normal, mod);
    if (!exitListener_) self->getLogger().error("failed to register ClientExitLevelEvent listener");
    tickListener_ = bus.emplaceListener<ll::event::world::ClientLevelTickEvent>([this](auto& event) { onTick(event); }, ll::event::EventPriority::Normal, mod);
    if (!tickListener_) self->getLogger().error("failed to register ClientLevelTickEvent listener");
    keyListener_ = bus.emplaceListener<ll::event::input::KeyInputEvent>([this](auto& event) { onKey(event); }, ll::event::EventPriority::Normal, mod);
    if (!keyListener_) self->getLogger().error("failed to register KeyInputEvent listener");
    if (!joinListener_ || !exitListener_ || !tickListener_ || !keyListener_) {
        self->getLogger().error(
            "voicechat client listener registration failed: join={} exit={} tick={} key={} eventIds=[{}|{}|{}|{}]",
            static_cast<bool>(joinListener_),
            static_cast<bool>(exitListener_),
            static_cast<bool>(tickListener_),
            static_cast<bool>(keyListener_),
            ll::event::getEventId<ll::event::client::ClientJoinLevelEvent>.name,
            ll::event::getEventId<ll::event::client::ClientExitLevelEvent>.name,
            ll::event::getEventId<ll::event::world::ClientLevelTickEvent>.name,
            ll::event::getEventId<ll::event::input::KeyInputEvent>.name
        );
        shared::FileLog::error("enable: listener registration failed");
        disable();
        return false;
    }
    if (self) self->getLogger().info("voicechat client listeners enabled");
    shared::FileLog::info("enable: client listeners enabled");
    // UI 通道探针只做通道可用性取证，失败不影响语音主链路。
    if (!uiChannelProbe_.initialize()) {
        if (self) self->getLogger().warn("UI channel probe is unavailable; voice chat continues normally");
        shared::FileLog::warn("enable: UI channel probe is unavailable; voice chat continues normally");
    }
    return true;
}

bool ClientMod::disable() {
    uiChannelProbe_.shutdown();
    if (audioDevice_) audioDevice_->stop();
    if (runtime_) runtime_->stop();
    auto& bus = ll::event::EventBus::getInstance();
    if (joinListener_) { bus.removeListener<ll::event::client::ClientJoinLevelEvent>(joinListener_); joinListener_.reset(); }
    if (exitListener_) { bus.removeListener<ll::event::client::ClientExitLevelEvent>(exitListener_); exitListener_.reset(); }
    if (tickListener_) { bus.removeListener<ll::event::world::ClientLevelTickEvent>(tickListener_); tickListener_.reset(); }
    if (keyListener_) { bus.removeListener<ll::event::input::KeyInputEvent>(keyListener_); keyListener_.reset(); }
    return true;
}

bool ClientMod::unload() {
    disable();
    if (transport_) transport_->setMessageHandler({});
    runtime_.reset();
    audioDevice_.reset();
    clock_.reset();
    playerState_.reset();
    transport_.reset();
    dearOreUi_.shutdown();
    dearOreUiPath_.clear();
    shared::FileLog::info("unload: client mod unloaded");
    return true;
}

void ClientMod::onJoin(ll::event::client::ClientJoinLevelEvent& event) {
    shared::FileLog::info("onJoin: ClientJoinLevelEvent received, starting client runtime");
    playerState_->set(&event.player());
    runtime_.reset();
    runtime_ = std::make_unique<ClientRuntime>(*transport_, *playerState_, *clock_, config_);
    // 自检由服务端 /voicechat test 命令经 Control(SmokeTest) 请求，回调在主线程 tick 中触发。
    runtime_->setSmokeTestRequestHandler([this] { startSmokeTest(); });
    runtime_->setRenderSink([this](const float* samples, std::size_t count) {
        if (!samples || count == 0) return;
        if (!audioDevice_) return;
        audio::WasapiPcmFrame frame;
        frame.sampleRate = static_cast<uint32_t>(config_.audio.sampleRate);
        frame.channels = static_cast<uint16_t>(std::max(1, config_.audio.channels));
        frame.samples.assign(samples, samples + count);
        audioDevice_->enqueueRender(std::move(frame));
    });
    if (audioDevice_) {
        audioDevice_->setCaptureCallback([this](const audio::WasapiPcmFrame& frame) {
            if (runtime_ && !frame.samples.empty()) runtime_->submitPcm(frame.samples.data(), frame.samples.size());
        });
        if (!audioDevice_->start()) {
            auto const detail = audioDevice_->lastError();
            auto self = ll::mod::NativeMod::current();
            if (self) self->getLogger().warn("WASAPI audio unavailable; voice chat will remain silent ({})", detail);
            shared::FileLog::warn("onJoin: WASAPI audio unavailable; voice chat will remain silent (" + detail + ")");
        } else if (audioDevice_->captureActive() && audioDevice_->renderActive()) {
            shared::FileLog::info("onJoin: WASAPI capture/render started");
        } else if (!audioDevice_->captureActive()) {
            // 没有麦克风也要能听到别人：采集降级不影响播放。
            auto const detail = audioDevice_->lastError();
            auto self = ll::mod::NativeMod::current();
            if (self) self->getLogger().warn("WASAPI microphone unavailable, you can still hear others ({})", detail);
            shared::FileLog::warn(
                "onJoin: WASAPI microphone unavailable, you can still hear others (" + detail + ")"
            );
        } else {
            auto const detail = audioDevice_->lastError();
            auto self = ll::mod::NativeMod::current();
            if (self) self->getLogger().warn("WASAPI playback unavailable, others can still hear you ({})", detail);
            shared::FileLog::warn(
                "onJoin: WASAPI playback unavailable, others can still hear you (" + detail + ")"
            );
        }
    }
    runtime_->start();
    shared::FileLog::info(
        "onJoin: client runtime started; smoke test is now armed, run /voicechat test on the server to start it"
    );
}

void ClientMod::startSmokeTest() {
    if (!runtime_) {
        shared::FileLog::warn("startSmokeTest: client runtime is not running; join a world first");
        return;
    }
    smokeTest_ = std::make_unique<SmokeTest>(*runtime_);
    smokeTestReported_ = false;
    smokeTest_->setLogSink([](bool isError, std::string const& message) {
        auto self = ll::mod::NativeMod::current();
        if (self) {
            if (isError) self->getLogger().warn("{}", message);
            else self->getLogger().info("{}", message);
        }
        if (isError) shared::FileLog::warn(message);
        else shared::FileLog::info(message);
    });
    smokeTest_->begin();
}

void ClientMod::onExit(ll::event::client::ClientExitLevelEvent&) {
    shared::FileLog::info("onExit: ClientExitLevelEvent received, stopping client runtime");
    smokeTest_.reset();
    if (audioDevice_) audioDevice_->stop();
    if (runtime_) runtime_->stop();
    runtime_.reset();
    playerState_->set(nullptr);
}

void ClientMod::onTick(ll::event::world::ClientLevelTickEvent&) {
    if (!runtime_) return;
    runtime_->tick();
    if (smokeTest_) {
        smokeTest_->tick(clock_->nowMs());
        if (smokeTest_->finished() && !smokeTestReported_) {
            smokeTestReported_ = true;
            // 设备级事实：真正写入渲染设备的帧数，用于区分“交给了 sink”和“真的出声”。
            if (audioDevice_) {
                auto const written = std::to_string(audioDevice_->renderFramesWritten());
                auto self = ll::mod::NativeMod::current();
                if (self) self->getLogger().info("[smoke] render device wrote {} frames", written);
                shared::FileLog::info("[smoke] render device wrote " + written + " frames");
            }
        }
    }
}

void ClientMod::onKey(ll::event::input::KeyInputEvent& event) {
    if (!runtime_ || event.keyCode() != static_cast<int>(config_.pttKey)) return;
    runtime_->setTalking(event.isDown());
}

} // namespace vc::client

namespace {
vc::client::ClientMod& clientMod() {
    static vc::client::ClientMod instance;
    return instance;
}
}

LL_REGISTER_MOD(vc::client::ClientMod, clientMod());
