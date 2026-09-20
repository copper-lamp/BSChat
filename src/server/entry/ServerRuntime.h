#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/config/Config.h"
#include "core/pipeline/ITransport.h"
#include "server/mixer/ServerMixer.h"
#include "server/session/SessionManager.h"
#include "server/stt/SherpaStt.h"

namespace vc::server {

class ServerRuntime final {
public:
    ServerRuntime(pipeline::ITransport& transport, config::ServerConfig config);
    ~ServerRuntime();

    ServerRuntime(const ServerRuntime&) = delete;
    ServerRuntime& operator=(const ServerRuntime&) = delete;

    void start();
    void stop();
    bool running() const { return running_; }

    void tickOnce(int64_t nowMs);
    void drainPending();
    void handleMessage(const protocol::PlayerId& peerId, const protocol::Message& message);

    size_t sessionCount() const { return sessions_.size(); }
    void removeSession(const protocol::PlayerId& id) { sessions_.removeSession(id); }
    const config::ServerConfig& config() const { return config_; }

private:
    void handleHello(const protocol::PlayerId& peerId, const protocol::HelloMessage& hello);
    void handleAudio(const protocol::PlayerId& peerId, const protocol::AudioDataMessage& audio);
    void handleControl(const protocol::PlayerId& peerId, const protocol::ControlMessage& control);
    PlayerSession::Options makeSessionOptions() const;
    std::unique_ptr<SherpaStt> createStt() const;

    pipeline::ITransport& transport_;
    config::ServerConfig config_;
    SessionManager sessions_;
    std::unique_ptr<SherpaStt> stt_;
    ServerMixer mixer_;
    bool running_ = false;
};

} // namespace vc::server
