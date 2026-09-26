#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "core/config/Config.h"
#include "core/pipeline/ITransport.h"
#include "server/mixer/ServerMixer.h"
#include "server/session/SessionManager.h"
#include "server/stt/SherpaStt.h"

namespace bsc::server {

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

    // 面板中继消息转发；本类保持零 LeviLamina 依赖，由外层宿主接上服务端中继。
    // 从网络线程回调（transport dispatch），订阅方需自行保证线程安全。
    using UiFormHandler = std::function<void(protocol::PlayerId const& peerId, protocol::UiFormMessage const&)>;
    void setUiFormHandler(UiFormHandler handler) { uiFormHandler_ = std::move(handler); }

    // 该玩家是否已完成 Hello/Welcome 会话登记（面板中继受理约束）。
    bool hasSession(protocol::PlayerId const& id) const { return static_cast<bool>(sessions_.find(id)); }

    // 管理员面板即时生效：开关服务器语音（config 为权威，mixer 侧每 tick 读取）。
    void setVoiceEnabled(bool enabled) { config_.voiceEnabled = enabled; }

    void tickOnce(int64_t nowMs);
    void drainPending();
    void handleMessage(const protocol::PlayerId& peerId, const protocol::Message& message);

    size_t sessionCount() const { return sessions_.size(); }
    void removeSession(const protocol::PlayerId& id);
    const config::ServerConfig& config() const { return config_; }

    // 真实音频回放自检：把一段 WAV 作为独立声源混入下行，供玩家听感验证传输质量。
    // 与真人语音走同一条「混音 → Opus 编码 → MixStream 下发」链路，只是不受自我抑制影响。
    bool playAudioFile(std::string const& path, std::string& error);
    void stopAudioFilePlayback();
    bool audioFilePlaying() const;
    std::string audioFileName() const;

private:
    struct SpeechActivity {
        int64_t startedAtMs = 0;
        uint64_t receivedFrames = 0;
        uint64_t acceptedFrames = 0;
        uint64_t rateLimitedFrames = 0;
        uint64_t lateFrames = 0;
        uint64_t duplicateFrames = 0;
        uint64_t invalidFrames = 0;
        uint64_t bufferFullFrames = 0;
        uint64_t evictedFrames = 0;
        uint64_t opusBytes = 0;
    };

    struct AudioDiagnostic {
        int64_t lastLoggedAtMs = -10000;
        uint64_t suppressedMessages = 0;
        uint64_t suppressedBytes = 0;
    };

    void handleHello(const protocol::PlayerId& peerId, const protocol::HelloMessage& hello);
    void handleAudio(const protocol::PlayerId& peerId, const protocol::AudioDataMessage& audio);
    void handleControl(const protocol::PlayerId& peerId, const protocol::ControlMessage& control);
    void finishSpeechActivity(const protocol::PlayerId& id, std::string const& reason, int64_t nowMs);
    void recordAudioDiagnostic(std::string const& reason, size_t bytes, int64_t nowMs);
    std::string formatSpeechSummary(const protocol::PlayerId& id, SpeechActivity const& activity, std::string const& reason, int64_t nowMs) const;
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
    std::mutex speechMutex_;
    std::mutex negotiatedCapabilitiesMutex_;
    std::map<protocol::PlayerId, uint8_t> negotiatedCapabilities_;
    std::map<protocol::PlayerId, SpeechActivity> speechActivities_;
    std::array<AudioDiagnostic, 3> audioDiagnostics_{};
    bool speechLoggingStopped_ = false;
    LogSink logSink_;
    UiFormHandler uiFormHandler_;
};

} // namespace bsc::server
