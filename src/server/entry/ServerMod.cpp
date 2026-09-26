#include "server/entry/ServerMod.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/i18n/I18n.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "ll/api/service/Bedrock.h"
#include "shared/util/FileLog.h"
#include "shared/util/PlayerIdUtils.h"

// LeviLamina 发布包由 MSVC 编译，内置事件 ID 取自 MSVC 的 __FUNCSIG__，形如
// "ll::event::player::PlayerJoinEvent"，保留 inline namespace 前缀（player/world/...）。
// 本模组由 clang-cl 编译，__PRETTY_FUNCTION__ 会省略 inline namespace，得到
// "ll::event::PlayerJoinEvent"。两者 FNV1a 哈希不同，EventBus 中不存在对应事件条目，
// addListener 会直接返回 false，导致启用阶段所有监听器注册失败。
// 这里把 getEventId 显式绑定到 SDK 侧的规范 ID，使 emplaceListener/removeListener
// 命中 LeviLamina.dll 已注册的事件条目。事件条目本身仍由 SDK 的 hook 型 emitter
// 创建并转发游戏事件，此处不做任何替代实现。
namespace ll::event {
template <>
constexpr EventIdView getEventId<player::PlayerJoinEvent> =
    EventIdView{"ll::event::player::PlayerJoinEvent"};
template <>
constexpr EventIdView getEventId<player::PlayerDisconnectEvent> =
    EventIdView{"ll::event::player::PlayerDisconnectEvent"};
template <>
constexpr EventIdView getEventId<world::ServerLevelTickEvent> =
    EventIdView{"ll::event::world::ServerLevelTickEvent"};
} // namespace ll::event

namespace bsc::server {

// /bsc play <file> 的命令参数结构（成员名即参数名）。
// 注意：必须定义在匿名命名空间之外——boost::pfr 反射依赖类型具有外部链接。
struct PlayAudioParams {
    std::string file;
};

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

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

bool ServerMod::load() {
    auto self = ll::mod::NativeMod::current();
    if (!self) return false;

    // 纯文本日志与宿主日志并行输出，方便在没有控制台的场景下取证。
    logPath_ = self->getConfigDir() / "bschat-server.log";
    shared::FileLog::reset(logPath_.string(), "bschat server log started");
    shared::FileLog::info("load: server mod loading, config dir = " + self->getConfigDir().string());

    // 管理员面板文案走 ll::i18n；语言文件缺失时各调用点回落到键名，不会阻塞加载。
    if (auto loaded = ll::i18n::getInstance().load(self->getLangDir()); !loaded) {
        shared::FileLog::warn("load: server language files unavailable, falling back to i18n keys");
    }

    configPath_ = self->getConfigDir() / "bschat.json";
    if (!loadConfig()) {
        shared::FileLog::error("load: config load failed at " + configPath_.string());
        return false;
    }

    transport_ = std::make_unique<shared::GamePacketTransport>(
        shared::TransportMode::Server,
        [this](const protocol::PlayerId& id) { return resolvePlayer(id); }
    );
    transport_->setLogSink([](bool isError, std::string const& message) {
        auto self = ll::mod::NativeMod::current();
        if (self) {
            if (isError) self->getLogger().warn("{}", message);
            else self->getLogger().info("{}", message);
        }
        if (isError) shared::FileLog::warn(message);
        else shared::FileLog::info(message);
    });
    runtime_ = std::make_unique<ServerRuntime>(*transport_, config_);
    runtime_->setLogSink([](bool isError, std::string const& message) {
        auto self = ll::mod::NativeMod::current();
        if (self) {
            if (isError) self->getLogger().warn("{}", message);
            else self->getLogger().info("{}", message);
        }
        if (isError) shared::FileLog::warn(message);
        else shared::FileLog::info(message);
    });

    // 面板中继：网络线程把 UiForm(Request) 转给中继，主线程在 tick 中投递给请求者本人。
    runtime_->setUiFormHandler([this](protocol::PlayerId const& peerId, protocol::UiFormMessage const& message) {
        if (panelRelay_) panelRelay_->handleRequest(peerId, message);
    });

    shared::FileLog::info("load: complete");
    return true;
}

bool ServerMod::enable() {
    auto self = ll::mod::NativeMod::current();
    if (!runtime_ || !transport_) {
        if (self) self->getLogger().error("bschat server enable aborted: runtime or transport is not loaded");
        shared::FileLog::error("enable: runtime or transport is not loaded");
        return false;
    }
    shared::FileLog::info("enable: registering server event listeners");

    auto& bus = ll::event::EventBus::getInstance();

    // 事件条目由 LeviLamina.dll 的 hook 型 emitter 在加载期注册。
    // 本模组不注册 emitter，只注册监听器；事件 ID 见文件头部的 getEventId 绑定。

    joinListener_ = bus.emplaceListener<ll::event::player::PlayerJoinEvent>(
        [this](auto& event) { onJoin(event); },
        ll::event::EventPriority::Normal,
        self
    );
    disconnectListener_ = bus.emplaceListener<ll::event::player::PlayerDisconnectEvent>(
        [this](auto& event) { onDisconnect(event); },
        ll::event::EventPriority::Normal,
        self
    );
    tickListener_ = bus.emplaceListener<ll::event::world::ServerLevelTickEvent>(
        [this](auto& event) { onTick(event); },
        ll::event::EventPriority::Normal,
        self
    );

    if (!joinListener_ || !disconnectListener_ || !tickListener_) {
        if (self) {
            self->getLogger().error(
                "bschat server listener registration failed: join={} disconnect={} tick={} eventIds=[{}|{}|{}]",
                static_cast<bool>(joinListener_),
                static_cast<bool>(disconnectListener_),
                static_cast<bool>(tickListener_),
                ll::event::getEventId<ll::event::player::PlayerJoinEvent>.name,
                ll::event::getEventId<ll::event::player::PlayerDisconnectEvent>.name,
                ll::event::getEventId<ll::event::world::ServerLevelTickEvent>.name
            );
        }
        shared::FileLog::error("enable: listener registration failed");
        disable();
        return false;
    }

    if (!registerCommand()) {
        if (self) self->getLogger().error("failed to register bschat smoke test command");
        shared::FileLog::error("enable: command registration failed");
        disable();
        return false;
    }

    runtime_->start();

    // 每次启用都重建面板中继（模组可被禁用后再启用，此时按当前配置目录重新加载面板定义）。
    panelRelay_ = std::make_unique<ui::PanelRelay>();
    panelRelay_->initialize(
        self->getModDir() / "panels",
        [this](protocol::PlayerId const& peerId, protocol::Message const& message) {
            if (transport_) transport_->send(peerId, message);
        },
        [this](protocol::PlayerId const& peerId) { return transport_ ? transport_->resolvePlayer(peerId) : nullptr; },
        [this](protocol::PlayerId const& peerId) { return runtime_ && runtime_->hasSession(peerId); },
        []() { return nowMs(); },
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

    if (self) self->getLogger().info("bschat server listeners enabled");
    shared::FileLog::info("enable: server listeners enabled");
    return true;
}

bool ServerMod::registerCommand() {
    auto self = ll::mod::NativeMod::current();
    if (!self) return false;

    // 专用服务器下客户端注册的命令会被服务端下发的命令表覆盖（实测提示“未知命令”），
    // 所以自检触发命令注册在服务端：服务端收到命令后，通过 Control(SmokeTest) 通知
    // 发起者自己的客户端开始端到端自检，结果仍只落在客户端日志里。
    auto& registrar = ll::command::CommandRegistrar::getServerInstance();
    auto& handle    = registrar.getOrCreateCommand(
        "bsc",
        "BSChat voice chat diagnostics",
        CommandPermissionLevel::Any,
        CommandFlagValue::NotCheat,
        self
    );
    handle.overload<>().text("test").execute([this](CommandOrigin const& origin, CommandOutput& output) {
        auto* entity = origin.getEntity();
        if (!entity || !entity->isPlayer()) {
            output.error("bsc: /bsc test must be run by a player in game");
            return;
        }
        auto& player = static_cast<Player&>(*entity);
        protocol::ControlMessage control;
        control.type = protocol::ControlType::SmokeTest;
        transport_->send(playerId(player), control);
        output.success("bsc: smoke test requested on your client, see the client log for results");
        shared::FileLog::info("command: smoke test requested by player");
    });
    // /bsc play <file>：把一段 WAV 当独立声源混入下行，用真实音频验证听感。
    // 文件优先按给定路径查找，否则取 <配置目录>/audio/<file>。
    handle.overload<PlayAudioParams>()
        .text("play")
        .required("file")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, PlayAudioParams const& params) {
            auto* entity = origin.getEntity();
            if (!entity || !entity->isPlayer()) {
                output.error("bsc: /bsc play must be run by a player in game");
                return;
            }
            auto const path = resolveAudioFile(params.file);
            if (path.empty()) {
                output.error(
                    "bsc: audio file not found; put a 48kHz wav under <bschat config>/audio/ "
                    "or pass an absolute path"
                );
                return;
            }
            std::string error;
            if (!runtime_->playAudioFile(path.string(), error)) {
                output.error("bsc: playback failed - " + error);
                shared::FileLog::error("command: audio playback failed: " + error);
                return;
            }
            output.success("bsc: playing " + path.filename().string() + " through the voice chat downlink");
            shared::FileLog::info("command: audio file playback started: " + path.string());
        });

    handle.overload<>().text("stop").execute([this](CommandOrigin const&, CommandOutput& output) {
        if (!runtime_->audioFilePlaying()) {
            output.success("bsc: no audio file playback is running");
            return;
        }
        auto const name = runtime_->audioFileName();
        runtime_->stopAudioFilePlayback();
        output.success("bsc: stopped playing " + name);
        shared::FileLog::info("command: audio file playback stopped: " + name);
    });

    // /bsc admin：仅管理员（OP）可执行；服务端本地构建管理面板并下发给本人。
    // 面板只暴露已存在的服务端配置项（服务器语音开关），提交后写回并落盘。
    handle.overload<>().text("admin").execute([this](CommandOrigin const& origin, CommandOutput& output) {
        auto* entity = origin.getEntity();
        if (!entity || !entity->isPlayer()) {
            output.error("bsc: /bsc admin must be run by a player in game");
            return;
        }
        auto& player = static_cast<Player&>(*entity);
        if (!player.isOperator()) {
            output.error("bsc: /bsc admin requires operator permission");
            shared::FileLog::warn("command: admin panel denied (not an operator)");
            return;
        }
        if (!panelRelay_) {
            output.error("bsc: admin panel is not available");
            return;
        }
        bool const opened = panelRelay_->openAdminPanel(player, config_, [this] {
            if (runtime_) runtime_->setVoiceEnabled(config_.voiceEnabled);
            if (!persistConfig()) shared::FileLog::warn("command: failed to persist the server config");
        });
        if (!opened) {
            output.error("bsc: admin panel is unavailable (panel definition missing)");
            return;
        }
        output.success("bsc: admin panel sent");
        shared::FileLog::info("command: admin panel sent to an operator");
    });

    smokeCommand_ = &handle;
    shared::FileLog::info("enable: registered server command /bsc test");
    return true;
}

std::filesystem::path ServerMod::resolveAudioFile(const std::string& name) const {
    std::error_code error;
    std::filesystem::path candidate(name);
    if (candidate.is_absolute() && std::filesystem::exists(candidate, error)) return candidate;
    if (!candidate.is_absolute() && std::filesystem::exists(candidate, error)) return candidate;
    auto inConfig = configPath_.parent_path() / "audio" / candidate;
    if (std::filesystem::exists(inConfig, error)) return inConfig;
    return {};
}

bool ServerMod::disable() {
    if (joinListener_) {
        ll::event::EventBus::getInstance().removeListener<ll::event::player::PlayerJoinEvent>(joinListener_);
        joinListener_.reset();
    }
    if (disconnectListener_) {
        ll::event::EventBus::getInstance().removeListener<ll::event::player::PlayerDisconnectEvent>(disconnectListener_);
        disconnectListener_.reset();
    }
    if (tickListener_) {
        ll::event::EventBus::getInstance().removeListener<ll::event::world::ServerLevelTickEvent>(tickListener_);
        tickListener_.reset();
    }
    if (runtime_) runtime_->stop();
    if (panelRelay_) panelRelay_->shutdown();
    players_.clear();
    if (transport_) transport_->clearPlayers(); // 避免停用后残留失效的 Player*
    smokeCommand_ = nullptr;
    return true;
}

bool ServerMod::unload() {
    disable();
    panelRelay_.reset();
    runtime_.reset();
    transport_.reset();
    shared::FileLog::info("unload: server mod unloaded");
    return true;
}

bool ServerMod::loadConfig() {
    std::error_code error;
    std::filesystem::create_directories(configPath_.parent_path(), error);
    auto text = readText(configPath_);
    if (text.empty()) {
        config_ = config::ServerConfig{};
        return writeText(configPath_, config::serverConfigToJson(config_));
    }
    config_ = config::serverConfigFromJson(text);
    return true;
}

bool ServerMod::persistConfig() {
    return writeText(configPath_, config::serverConfigToJson(config_));
}

void ServerMod::onJoin(ll::event::player::PlayerJoinEvent& event) {
    auto& player = event.self();
    auto const id = playerId(player);
    players_[id] = &player;
    shared::FileLog::info("onJoin: player joined, active players = " + std::to_string(players_.size()));
}

void ServerMod::onDisconnect(ll::event::player::PlayerDisconnectEvent& event) {
    auto id = playerId(event.self());
    players_.erase(id);
    if (transport_) transport_->forgetPlayer(id);
    if (runtime_) runtime_->removeSession(id);
    shared::FileLog::info("onDisconnect: player left, active players = " + std::to_string(players_.size()));
}

void ServerMod::onTick(ll::event::world::ServerLevelTickEvent&) {
    if (!runtime_ || !runtime_->running()) return;
    // 面板中继与管理员面板投递必须在游戏主线程完成。
    if (panelRelay_) panelRelay_->tick(nowMs());
    runtime_->tickOnce(nowMs());
    runtime_->drainPending();
}

Player* ServerMod::resolvePlayer(const protocol::PlayerId& id) const {
    auto it = players_.find(id);
    return it == players_.end() ? nullptr : it->second;
}

protocol::PlayerId ServerMod::playerId(const Player& player) const {
    return shared::playerIdFromUuid(player.getUuid());
}

} // namespace bsc::server

namespace {
bsc::server::ServerMod& serverMod() {
    static bsc::server::ServerMod instance;
    return instance;
}
}

LL_REGISTER_MOD(bsc::server::ServerMod, serverMod());
