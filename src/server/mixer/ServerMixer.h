#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <map>
#include <memory>

#include "core/audio/AudioTypes.h"
#include "core/audio/MixerCore.h"
#include "core/audio/SpatialPolicy.h"
#include "core/codec/OpusCodec.h"
#include "core/pipeline/IStt.h"
#include "core/protocol/Message.h"
#include "server/session/SessionManager.h"

namespace vc::server {

// 独立音频线程上的逐接收者混音器。STT 回调与音频输出共用待发队列。
class ServerMixer {
public:
    struct Config {
        int sampleRate = audio::kDefaultSampleRate;
        int channels = audio::kDefaultChannels;
        int frameSizeMs = audio::kDefaultFrameSizeMs;
        int bitrateKbps = audio::kDefaultBitrateKbps;
        int tickMs = audio::kMixTickMs;
        size_t maxPending = 1024;
        audio::SpatialPolicyConfig spatial;
        size_t maxTalkers = 0;
        size_t maxChatters = 0;
        int64_t staleMs = 2000;
    };

    explicit ServerMixer(SessionManager& sessions, Config config);
    ~ServerMixer();
    ServerMixer(const ServerMixer&) = delete;
    ServerMixer& operator=(const ServerMixer&) = delete;

    void start();
    void stop();
    bool running() const { return running_.load(); }
    void setStt(pipeline::IStt* stt);
    void tickOnce(int64_t nowMs);
    void drainPending(const std::function<void(const protocol::PlayerId&, const protocol::Message&)>& sendFn);
    uint64_t tickCount() const { return tickCount_.load(); }

private:
    void threadMain();
    void enqueueToAll(const protocol::Message& message);
    void enqueueTo(const protocol::PlayerId& peerId, const protocol::Message& message);

    SessionManager& sessions_;
    Config config_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tickCount_{0};
    pipeline::IStt* stt_ = nullptr;
    std::mutex pendingMutex_;
    std::deque<std::pair<protocol::PlayerId, protocol::Message>> pending_;
    audio::MixerCore mixer_;
    audio::SpatialPolicy spatialPolicy_;
    std::map<protocol::PlayerId, std::unique_ptr<codec::OpusEncoder>> encoders_;
    std::map<protocol::PlayerId, uint64_t> mixSeqs_;
};

} // namespace vc::server
