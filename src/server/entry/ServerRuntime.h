#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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

    // 诊断日志出口。ServerRuntime 本身零 LeviLamina 依赖（宿主测试需要），
    // 因此由外层宿主注入日志函数；未注入时全部诊断静默丢弃。
    using LogSink = std::function<void(bool isError, std::string const& message)>;
    void setLogSink(LogSink sink) { logSink_ = std::move(sink); }

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
    void logInfo(std::string const& message) const;
    void logWarn(std::string const& message) const;

    pipeline::ITransport& transport_;
    config::ServerConfig config_;
    SessionManager sessions_;
    std::unique_ptr<SherpaStt> stt_;
    ServerMixer mixer_;
    bool running_ = false;
    uint64_t acceptedAudioFrames_ = 0;
    uint64_t sentMixFrames_ = 0;
    LogSink logSink_;
};

} // namespace vc::server
