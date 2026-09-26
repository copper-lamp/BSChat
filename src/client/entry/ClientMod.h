#pragma once

#include <filesystem>
#include <memory>

#include "client/entry/ClientRuntime.h"
#include "client/audio/WasapiAudio.h"
#include "client/DearOreUiIntegration.h"
#include "client/entry/SmokeTest.h"
#include "shared/transport/GamePacketTransport.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

namespace vc::client {

class PlayerState final : public IPlayerState {
public:
    void set(class Player* player);
    protocol::PlayerId playerId() const override;
    Position position() const override;
private:
    class Player* player_ = nullptr;
};

class ClientMod final {
public:
    ClientMod() = default;
    explicit ClientMod(std::unique_ptr<ClientRuntime> runtime) : runtime_(std::move(runtime)) {}

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    void startSmokeTest();
    void onJoin(class ll::event::client::ClientJoinLevelEvent& event);
    void onExit(class ll::event::client::ClientExitLevelEvent& event);
    void onTick(class ll::event::world::ClientLevelTickEvent& event);
    void onKey(class ll::event::input::KeyInputEvent& event);

    std::filesystem::path logPath_;
    std::unique_ptr<ClientRuntime> runtime_;
    std::unique_ptr<SmokeTest> smokeTest_;
    bool smokeTestReported_ = false;
    std::unique_ptr<audio::WasapiAudioDevice> audioDevice_;
    std::unique_ptr<shared::GamePacketTransport> transport_;
    std::unique_ptr<PlayerState> playerState_;
    std::unique_ptr<IClock> clock_;
    config::ClientConfig config_;
    std::wstring dearOreUiPath_;
    DearOreUiIntegration dearOreUi_;
    std::shared_ptr<ll::event::Listener<ll::event::client::ClientJoinLevelEvent>> joinListener_;
    std::shared_ptr<ll::event::Listener<ll::event::client::ClientExitLevelEvent>> exitListener_;
    std::shared_ptr<ll::event::Listener<ll::event::world::ClientLevelTickEvent>> tickListener_;
    std::shared_ptr<ll::event::Listener<ll::event::input::KeyInputEvent>> keyListener_;
};

} // namespace vc::client
