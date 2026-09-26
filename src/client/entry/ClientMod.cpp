#include "client/entry/ClientMod.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "client/entry/ClientEventIds.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/i18n/I18n.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/world/actor/player/Player.h"
#include "shared/transport/GamePacketTransport.h"
#include "shared/util/FileLog.h"
#include "shared/util/PlayerIdUtils.h"

namespace bsc::client {

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

    // 面向玩家的文案（HUD 状态、面板标题与选项）统一走 ll::i18n；
    // 语言文件缺失时各调用点回落到键名，不会阻塞加载。
    if (auto loaded = ll::i18n::getInstance().load(self->getLangDir()); !loaded) {
        shared::FileLog::warn("load: client language files unavailable, falling back to i18n keys");
    }

    // 纯文本日志与宿主日志并行输出，方便在没有控制台的场景下取证。
    logPath_ = self->getConfigDir() / "bschat-client.log";
    shared::FileLog::reset(logPath_.string(), "bschat client log started");
    shared::FileLog::info("load: client mod loading, config dir = " + self->getConfigDir().string());

    auto path = self->getConfigDir() / "bschat.json";
    auto text = readText(path);
    config::ClientConfig config{};
    if (!text.empty()) config = config::clientConfigFromJson(text);
    else {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (!writeText(path, config::clientConfigToJson(config))) return false;
    }

    transport_ = std::make_unique<shared::GamePacketTransport>(shared::TransportMode::Client);
    playerState_ = std::make_unique<PlayerState>();
    clock_ = std::make_unique<SteadyClock>();
    config_ = std::move(config);
    // 音频管线格式由配置决定，WASAPI 两端都按它 Initialize，由引擎转换到端点实际格式。
    // 帧长不在这里固定：渲染写长度跟随到达帧（服务端协商帧长）。
    audio::WasapiAudioConfig audioConfig;
    audioConfig.sampleRate = static_cast<uint32_t>(std::max(8000, config_.audio.sampleRate));
    audioConfig.channels = static_cast<uint16_t>(std::max(1, config_.audio.channels));
    audioDevice_ = std::make_unique<audio::WasapiAudioDevice>(audioConfig);
    shared::FileLog::info("load: complete");
    return true;
}

bool ClientMod::enable() {
    auto self = ll::mod::NativeMod::current();
    if (!transport_ || !playerState_ || !clock_) {
        if (self) self->getLogger().error("bschat client enable prerequisites are not ready");
        shared::FileLog::error("enable: prerequisites are not ready (transport/playerState/clock)");
        return false;
    }
    shared::FileLog::info("enable: registering client event listeners");
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
            "bschat client listener registration failed: join={} exit={} tick={} key={} eventIds=[{}|{}|{}|{}]",
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
    if (self) self->getLogger().info("bschat client listeners enabled");
    shared::FileLog::info("enable: client listeners enabled");
    hudLayer_.applyConfig(config_);
    // 状态图标 PNG 随模组发布在模组目录的 icons/（由构建脚本拷入），运行期解码后直接上传进纹理组。
    hudLayer_.setIconDirectory(self->getModDir() / "icons");
    if (!hudLayer_.initialize()) {
        if (self) self->getLogger().warn("HUD layer is unavailable; voice chat continues normally");
        shared::FileLog::warn("enable: HUD layer is unavailable; voice chat continues normally");
    }

    // 面板定义随模组发布在模组目录的 panels/（与 lang/ 同级，由构建脚本拷入）。
    panelRelay_ = std::make_unique<ui::PanelRelay>();
    panelRelay_->initialize(
        self->getModDir() / "panels",
        [this](protocol::Message const& message) {
            if (transport_) transport_->send({}, message);
        },
        [this]() -> config::ClientConfig const& { return config_; },
        [this](config::ClientConfig const& config) { applyClientConfig(config); },
        [this]() { return clock_ ? clock_->nowMs() : 0; },
        [](bool isError, std::string const& message) {
            auto self = ll::mod::NativeMod::current();
            if (self) {
                if (isError) self->getLogger().warn("{}", message);
                else self->getLogger().info("{}", message);
            }
            if (isError) shared::FileLog::warn(message);
            else shared::FileLog::info(message);
        }
    );
    return true;
}

bool ClientMod::disable() {
    hudLayer_.shutdown();
    if (panelRelay_) panelRelay_->shutdown();
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
    panelRelay_.reset();
    runtime_.reset();
    audioDevice_.reset();
    clock_.reset();
    playerState_.reset();
    transport_.reset();
    shared::FileLog::info("unload: client mod unloaded");
    return true;
}

void ClientMod::onJoin(ll::event::client::ClientJoinLevelEvent& event) {
    shared::FileLog::info("onJoin: ClientJoinLevelEvent received, starting client runtime");
    playerState_->set(&event.player());
    runtime_.reset();
    runtime_ = std::make_unique<ClientRuntime>(*transport_, *playerState_, *clock_, config_);
    // 自检由服务端 /bsc test 命令经 Control(SmokeTest) 请求，回调在主线程 tick 中触发。
    runtime_->setSmokeTestRequestHandler([this] { startSmokeTest(); });
    // 字幕由服务端 STT 结果驱动，入队后由 HUD 在渲染事件里绘制。
    runtime_->setSttTextHandler([this](protocol::SttTextMessage const& text) {
        if (clock_) hudLayer_.pushSttText(text, clock_->nowMs());
    });
    // 面板中继响应：网络线程只入队，主线程 tick 里处理。
    runtime_->setUiFormHandler([this](protocol::UiFormMessage const& message) {
        if (panelRelay_) panelRelay_->handleMessage(message);
    });
    hudLayer_.applyConfig(config_);
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
            if (!config_.captureEnabled) return; // 采集开关：关闭后只收听
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
        "onJoin: client runtime started; smoke test is now armed, run /bsc test on the server to start it"
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

void ClientMod::applyClientConfig(config::ClientConfig const& config) {
    bool const pttKeyChanged = config.pttKey != config_.pttKey;
    config_ = config;
    hudLayer_.applyConfig(config_);
    if (runtime_) runtime_->setOutputVolume(config_.playbackVolume);
    // 说话键被改掉时，旧键的“松开”事件再也不会到达，必须主动结束上行，否则会一直占着麦克风。
    if (pttKeyChanged && runtime_) runtime_->setTalking(false);

    auto self = ll::mod::NativeMod::current();
    if (!self) return;
    if (!writeText(self->getConfigDir() / "bschat.json", config::clientConfigToJson(config_))) {
        shared::FileLog::warn("applyClientConfig: failed to persist the client config");
    }
}

void ClientMod::updateHudStatus() {
    if (!runtime_) return;
    int64_t const now = clock_ ? clock_->nowMs() : 0;

    // 「正在放音」以近 500ms 内是否收到过下行混音帧判定，避免逐帧计数抖动。
    uint64_t const frames = runtime_->receivedMixFrames();
    if (frames != lastMixFrames_) {
        lastMixFrames_ = frames;
        lastMixFrameMs_ = now;
    }

    hud::StatusInputs inputs;
    inputs.inSession = runtime_->state() == ClientRuntime::State::Ready;
    inputs.talking = runtime_->talking();
    inputs.muted = false; // 本端麦克风静音尚未实现，预留状态位
    inputs.playing = lastMixFrameMs_ != 0 && (now - lastMixFrameMs_) < 500;
    hudLayer_.setStatusInputs(inputs);
}

void ClientMod::onExit(ll::event::client::ClientExitLevelEvent&) {
    shared::FileLog::info("onExit: ClientExitLevelEvent received, stopping client runtime");
    hudLayer_.clearSubtitles();
    smokeTest_.reset();
    if (audioDevice_) audioDevice_->stop();
    if (runtime_) runtime_->stop();
    runtime_.reset();
    playerState_->set(nullptr);
}

void ClientMod::onTick(ll::event::world::ClientLevelTickEvent&) {
    if (panelRelay_ && clock_) panelRelay_->tick(clock_->nowMs());
    if (!runtime_) return;
    runtime_->tick();
    updateHudStatus();
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
    int const key = event.keyCode();
    // 设置面板快捷键（默认 J）：按下即请求服务端中继下发设置面板。
    if (key == static_cast<int>(config_.settingsKey)) {
        if (event.isDown() && panelRelay_) panelRelay_->requestPanel("bschat.settings.client");
        return;
    }
    if (!runtime_ || key != static_cast<int>(config_.pttKey)) return;
    runtime_->setTalking(event.isDown());
}

} // namespace bsc::client

namespace {
bsc::client::ClientMod& clientMod() {
    static bsc::client::ClientMod instance;
    return instance;
}
}

LL_REGISTER_MOD(bsc::client::ClientMod, clientMod());
