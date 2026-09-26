#include "client/entry/ClientMod.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/world/actor/player/Player.h"
#include "shared/transport/GamePacketTransport.h"
#include "shared/util/FileLog.h"
#include "shared/util/PlayerIdUtils.h"

// LeviLamina 发布包由 MSVC 编译，内置事件 ID 取自 MSVC 的 __FUNCSIG__，形如
// "ll::event::client::ClientJoinLevelEvent"，保留 inline namespace 前缀（client/world/input）。
// 本模组由 clang-cl 编译，__PRETTY_FUNCTION__ 会省略 inline namespace，得到
// "ll::event::ClientJoinLevelEvent"。两者 FNV1a 哈希不同，EventBus 中不存在对应事件条目，
// addListener 会直接返回 false，导致启用阶段所有监听器注册失败。
// 这里把 getEventId 显式绑定到 SDK 侧的规范 ID，使 emplaceListener/removeListener
// 命中 LeviLamina.dll 已注册的事件条目。事件条目本身仍由 SDK 的 hook 型 emitter
// 创建并转发游戏事件，此处不做任何替代实现。
namespace ll::event {
template <>
constexpr EventIdView getEventId<client::ClientJoinLevelEvent> =
    EventIdView{"ll::event::client::ClientJoinLevelEvent"};
template <>
constexpr EventIdView getEventId<client::ClientExitLevelEvent> =
    EventIdView{"ll::event::client::ClientExitLevelEvent"};
template <>
constexpr EventIdView getEventId<world::ClientLevelTickEvent> =
    EventIdView{"ll::event::world::ClientLevelTickEvent"};
template <>
constexpr EventIdView getEventId<input::KeyInputEvent> =
    EventIdView{"ll::event::input::KeyInputEvent"};
} // namespace ll::event

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
    audioDevice_ = std::make_unique<audio::WasapiAudioDevice>();
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
    if (!registerCommand()) {
        if (self) self->getLogger().error("failed to register voicechat smoke test command");
        shared::FileLog::error("enable: command registration failed");
        disable();
        return false;
    }
    return true;
}

bool ClientMod::disable() {
    if (audioDevice_) audioDevice_->stop();
    if (runtime_) runtime_->stop();
    auto& bus = ll::event::EventBus::getInstance();
    if (joinListener_) { bus.removeListener<ll::event::client::ClientJoinLevelEvent>(joinListener_); joinListener_.reset(); }
    if (exitListener_) { bus.removeListener<ll::event::client::ClientExitLevelEvent>(exitListener_); exitListener_.reset(); }
    if (tickListener_) { bus.removeListener<ll::event::world::ClientLevelTickEvent>(tickListener_); tickListener_.reset(); }
    if (keyListener_) { bus.removeListener<ll::event::input::KeyInputEvent>(keyListener_); keyListener_.reset(); }
    smokeCommand_ = nullptr;
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
            auto self = ll::mod::NativeMod::current();
            if (self) self->getLogger().warn("WASAPI audio unavailable; voice chat will remain silent");
            shared::FileLog::warn("onJoin: WASAPI audio unavailable; voice chat will remain silent");
        } else {
            shared::FileLog::info("onJoin: WASAPI capture/render started");
        }
    }
    runtime_->start();
    shared::FileLog::info(
        "onJoin: client runtime started; smoke test is now armed, type /voicechat test to run it"
    );
}

bool ClientMod::registerCommand() {
    auto self = ll::mod::NativeMod::current();
    if (!self) return false;

    // 客户端命令：进入服务器后由玩家手动输入 /voicechat test 触发双端链路自检。
    auto& registrar = ll::command::CommandRegistrar::getClientInstance();
    auto& handle = registrar.getOrCreateCommand(
        "voicechat",
        "Betterlanguagechat voice chat diagnostics",
        CommandPermissionLevel::Any,
        CommandFlagValue::NotCheat,
        self
    );
    handle.overload<>().text("test").execute([this](CommandOrigin const&, CommandOutput& output) {
        output.success("voicechat: starting end-to-end smoke test, see the client log for results");
        startSmokeTest();
    });
    smokeCommand_ = &handle;
    shared::FileLog::info("enable: registered client command /voicechat test");
    return true;
}

void ClientMod::startSmokeTest() {
    if (!runtime_) {
        shared::FileLog::warn("startSmokeTest: client runtime is not running; join a world first");
        return;
    }
    smokeTest_ = std::make_unique<SmokeTest>(*runtime_);
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
    if (smokeTest_) smokeTest_->tick(clock_->nowMs());
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
