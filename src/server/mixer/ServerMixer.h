#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/audio/GlobalMixer.h"
#include "core/codec/OpusCodec.h"
#include "core/protocol/Message.h"
#include "server/session/SessionManager.h"

namespace vc::server {

// 服务端混音器：独立音频线程（120ms tick），不阻塞 BDS 主线程。
// 每 tick：拉各会话解码帧 → 全局混音 → 低码率 Opus 编码 → 待发队列；
// 主线程 drainPending() 排空并通过 transport 推送 MixStream。
// 无活跃发言 → 整 tick 不推流（静默期零带宽）。
//
// 线程模型：
//  - 音频线程：tickOnce()（拉帧/混音/编码/入队 + 完成话语回调 STT）；
//  - 主线程：drainPending()、start()/stop()、setUtteranceSink()。
// 零 LeviLamina 依赖（混音器/编码器位于 core），tickOnce 可直接 host 单测。
class ServerMixer {
public:
    struct Config {
        int sampleRate = audio::kDefaultSampleRate;
        int channels = 1;
        int frameSizeMs = audio::kDefaultFrameSizeMs;
        int bitrateKbps = audio::kDefaultBitrateKbps;
        int tickMs = audio::kMixTickMs; // 120
        size_t maxPending = 1024;       // 待发队列上限：主线程排空不及时时丢最旧
    };

    // 完成话语回调（音频线程触发）：供 STT 提交
    using UtteranceSink = std::function<void(const protocol::PlayerId&, std::vector<float>)>;

    explicit ServerMixer(SessionManager& sessions, Config config);
    ~ServerMixer();
    ServerMixer(const ServerMixer&) = delete;
    ServerMixer& operator=(const ServerMixer&) = delete;

    void start();
    void stop();
    bool running() const { return running_.load(); }

    void setUtteranceSink(UtteranceSink sink);

    // 手动跑一个 tick（音频线程内部循环与 host 单测共用）
    void tickOnce(int64_t nowMs);

    // 主线程：排空待发队列，逐个交给 sendFn
    void drainPending(
        const std::function<void(const protocol::PlayerId&, const protocol::Message&)>& sendFn
    );

    uint64_t tickCount() const { return tickCount_.load(); }

private:
    void threadMain();
    void enqueueToAll(const protocol::Message& message);

    SessionManager& sessions_;
    Config config_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tickCount_{0};

    std::mutex sinkMutex_;
    UtteranceSink utteranceSink_;

    std::mutex pendingMutex_;
    std::deque<std::pair<protocol::PlayerId, protocol::Message>> pending_;

    // 以下仅音频线程访问
    audio::GlobalMixer mixer_;
    codec::OpusEncoder encoder_;
    uint64_t mixSeq_ = 0;
};

} // namespace vc::server
