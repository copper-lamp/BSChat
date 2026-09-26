#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "core/config/Config.h"
#include "core/protocol/Message.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "server/entry/ServerRuntime.h"
#include "server/ui/PanelRelay.h"
#include "shared/transport/GamePacketTransport.h"

class Player;

namespace bsc::server {

class ServerMod final {
public:
    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    bool loadConfig();
    bool registerCommand();
    // 把当前 config_ 落盘（沿用 loadConfig 的写文件方式）。
    bool persistConfig();
    // 解析 /bsc play 的文件参数：优先绝对路径，其次 <配置目录>/audio/<name>。
    std::filesystem::path resolveAudioFile(const std::string& name) const;
    void onJoin(ll::event::player::PlayerJoinEvent& event);
    void onDisconnect(ll::event::player::PlayerDisconnectEvent& event);
    void onTick(ll::event::world::ServerLevelTickEvent& event);
    Player* resolvePlayer(const protocol::PlayerId& id) const;
    protocol::PlayerId playerId(const Player& player) const;

    std::filesystem::path configPath_;
    std::filesystem::path logPath_;
    config::ServerConfig config_;
    std::unique_ptr<shared::GamePacketTransport> transport_;
    std::unique_ptr<ServerRuntime> runtime_;
    std::unique_ptr<ui::PanelRelay> panelRelay_;
    std::map<protocol::PlayerId, Player*> players_;
    ll::command::CommandHandle* smokeCommand_ = nullptr;
    std::shared_ptr<ll::event::Listener<ll::event::player::PlayerJoinEvent>> joinListener_;
    std::shared_ptr<ll::event::Listener<ll::event::player::PlayerDisconnectEvent>> disconnectListener_;
    std::shared_ptr<ll::event::Listener<ll::event::world::ServerLevelTickEvent>> tickListener_;
};

} // namespace bsc::server
