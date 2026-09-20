#include "client/entry/ClientMod.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "ll/api/event/EventBus.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/world/actor/player/Player.h"
#include "shared/transport/GamePacketTransport.h"
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
    if (dearOreUi_.initialize(dearOreUiPath.wstring()) && self) self->getLogger().info("DearOreUI Settings integration initialized");
    playerState_ = std::make_unique<PlayerState>();
    clock_ = std::make_unique<SteadyClock>();
    config_ = std::move(config);
    return true;
}

bool ClientMod::enable() {
    auto self = ll::mod::NativeMod::current();
    if (!transport_ || !playerState_ || !clock_) {
        if (self) self->getLogger().error("voicechat client enable prerequisites are not ready");
        return false;
    }
    auto& bus = ll::event::EventBus::getInstance();
    auto mod = std::weak_ptr<ll::mod::Mod>(self);
    joinListener_ = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>([this](auto& event) { onJoin(event); }, ll::event::EventPriority::Normal, mod);
    if (!joinListener_ && self) self->getLogger().error("failed to register ClientJoinLevelEvent listener");
    exitListener_ = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>([this](auto& event) { onExit(event); }, ll::event::EventPriority::Normal, mod);
    if (!exitListener_ && self) self->getLogger().error("failed to register ClientExitLevelEvent listener");
    tickListener_ = bus.emplaceListener<ll::event::world::ClientLevelTickEvent>([this](auto& event) { onTick(event); }, ll::event::EventPriority::Normal, mod);
    if (!tickListener_ && self) self->getLogger().error("failed to register ClientLevelTickEvent listener");
    keyListener_ = bus.emplaceListener<ll::event::input::KeyInputEvent>([this](auto& event) { onKey(event); }, ll::event::EventPriority::Normal, mod);
    if (!keyListener_ && self) self->getLogger().error("failed to register KeyInputEvent listener");
    if (!joinListener_ || !exitListener_ || !tickListener_ || !keyListener_) {
        disable();
        return false;
    }
    if (self) self->getLogger().info("voicechat client listeners enabled");
    return true;
}

bool ClientMod::disable() {
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
    clock_.reset();
    playerState_.reset();
    transport_.reset();
    dearOreUi_.shutdown();
    return true;
}

void ClientMod::onJoin(ll::event::client::ClientJoinLevelEvent& event) {
    playerState_->set(&event.player());
    runtime_.reset();
    runtime_ = std::make_unique<ClientRuntime>(*transport_, *playerState_, *clock_, config_);
    runtime_->start();
}

void ClientMod::onExit(ll::event::client::ClientExitLevelEvent&) {
    if (runtime_) runtime_->stop();
    runtime_.reset();
    playerState_->set(nullptr);
}

void ClientMod::onTick(ll::event::world::ClientLevelTickEvent&) {
    if (runtime_) runtime_->tick();
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
