#include "server/entry/ServerMod.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/EmitterBase.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/server/ServerPlayer.h"
#include "ll/api/service/Bedrock.h"
#include "shared/util/PlayerIdUtils.h"

namespace vc::server {

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

    configPath_ = self->getConfigDir() / "voicechat.json";
    if (!loadConfig()) return false;

    transport_ = std::make_unique<shared::GamePacketTransport>(
        shared::TransportMode::Server,
        [this](const protocol::PlayerId& id) { return resolvePlayer(id); }
    );
    runtime_ = std::make_unique<ServerRuntime>(*transport_, config_);
    runtime_->setLogSink([](bool isError, std::string const& message) {
        auto self = ll::mod::NativeMod::current();
        if (!self) return;
        if (isError) self->getLogger().warn("{}", message);
        else self->getLogger().info("{}", message);
    });
    return true;
}

bool ServerMod::enable() {
    auto self = ll::mod::NativeMod::current();
    if (!runtime_ || !transport_) {
        if (self) self->getLogger().error("voicechat server enable aborted: runtime or transport is not loaded");
        return false;
    }

    auto& bus = ll::event::EventBus::getInstance();

    // LeviLamina only creates an event's stream when something registers it first.
    // Register a stream factory for any stream this SDK build did not create
    // eagerly, otherwise every listener below silently returns null.
    auto ensureEventStream = [&]<typename Event>() {
        if (!bus.hasEvent(ll::event::getEventId<Event>)) {
            bus.setEventEmitter<Event>([] { return std::make_unique<ll::event::EmitterBase>(); }, self);
        }
    };
    ensureEventStream.operator()<ll::event::player::PlayerJoinEvent>();
    ensureEventStream.operator()<ll::event::player::PlayerDisconnectEvent>();
    ensureEventStream.operator()<ll::event::world::ServerLevelTickEvent>();

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
                "voicechat server listener registration failed: join={} disconnect={} tick={}",
                static_cast<bool>(joinListener_),
                static_cast<bool>(disconnectListener_),
                static_cast<bool>(tickListener_)
            );
        }
        disable();
        return false;
    }

    runtime_->start();
    if (self) self->getLogger().info("voicechat server listeners enabled");
    return true;
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
    players_.clear();
    return true;
}

bool ServerMod::unload() {
    disable();
    runtime_.reset();
    transport_.reset();
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

void ServerMod::onJoin(ll::event::player::PlayerJoinEvent& event) {
    auto& player = event.self();
    players_[playerId(player)] = &player;
}

void ServerMod::onDisconnect(ll::event::player::PlayerDisconnectEvent& event) {
    auto id = playerId(event.self());
    players_.erase(id);
    if (runtime_) runtime_->removeSession(id);
}

void ServerMod::onTick(ll::event::world::ServerLevelTickEvent&) {
    if (!runtime_ || !runtime_->running()) return;
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

} // namespace vc::server

namespace {
vc::server::ServerMod& serverMod() {
    static vc::server::ServerMod instance;
    return instance;
}
}

LL_REGISTER_MOD(vc::server::ServerMod, serverMod());
