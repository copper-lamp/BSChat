#include "server/entry/ServerMod.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include "ll/api/event/EventBus.h"
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
    return true;
}

bool ServerMod::enable() {
    if (!runtime_ || !transport_) return false;

    auto& bus = ll::event::EventBus::getInstance();
    joinListener_ = bus.emplaceListener<ll::event::player::PlayerJoinEvent>(
        [this](auto& event) { onJoin(event); }
    );
    disconnectListener_ = bus.emplaceListener<ll::event::player::PlayerDisconnectEvent>(
        [this](auto& event) { onDisconnect(event); }
    );
    tickListener_ = bus.emplaceListener<ll::event::world::ServerLevelTickEvent>(
        [this](auto& event) { onTick(event); }
    );
    if (!joinListener_ || !disconnectListener_ || !tickListener_) {
        disable();
        return false;
    }

    runtime_->start();
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
