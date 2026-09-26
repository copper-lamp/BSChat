#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "core/protocol/Message.h"

#include "core/audio/AudioTypes.h"
#include "core/audio/JitterBuffer.h"
#include "core/codec/OpusCodec.h"

namespace bsc::server {

// 单个玩家的服务端会话：每发送者一个 Opus 解码器 + 抖动缓冲 + 限速。
// 零 LeviLamina 依赖，可纯 host 单测。
//
// 线程模型：
//  - pushAudio()    ：网络线程（包到达），互斥锁保护，仅入抖动缓冲与限速窗口；
//  - pollFrame()    ：音频线程，取一帧解码 PCM + 帧标志（Start/End），
//                     音频线程据此驱动流式 STT（begin/feed/end）；
// 解码器仅由音频线程使用，天然单线程。
//
// 话语累积/切分不在此处（v1 由流式 STT 侧按 Start/End 标志自行切分）。
class PlayerSession {
public:
    struct Options {
        int sampleRate = audio::kDefaultSampleRate;
        int channels = audio::kDefaultChannels;
        int frameSizeMs = audio::kDefaultFrameSizeMs;
        size_t maxFramesPerSecond = 40; // 客户端 ~16.7fps（60ms/帧），宽松上限防刷包
        size_t maxDepthFrames = 6;      // 抖动缓冲深度（帧）
        int64_t maxWaitMs = 200;        // 抖动缓冲队首等待（ms）
    };

    // 出帧结果：解码 PCM + 随帧标志（AudioFlag 位或，供话语切分驱动）
    struct FrameOut {
        std::vector<float> pcm; // 解码后 PCM（float [-1,1]）；空 = 静音/解码失败
        uint8_t flags = 0;      // protocol::AudioFlag 位或
    };

    explicit PlayerSession(protocol::PlayerId id, Options options);
    ~PlayerSession();
    PlayerSession(const PlayerSession&) = delete;
    PlayerSession& operator=(const PlayerSession&) = delete;

    const protocol::PlayerId& id() const { return id_; }
    const Options& options() const { return options_; }

    // 网络线程：接收一帧上行语音（seq/flags/opusData）；nowMs 为稳态时钟毫秒。
    void pushAudio(const protocol::AudioDataMessage& msg, int64_t nowMs);

    // 音频线程：取下一帧解码后 PCM 与帧标志；无帧返回 nullopt。
    // pcm 为空表示该帧为静音（无数据），混音按静音处理，但仍需驱动 STT 标志。
    std::optional<FrameOut> pollFrame(int64_t nowMs);

    // 会话复位（重连/参数变更）：清抖动缓冲与解码器状态。
    void reset();

    // 缓冲中待处理帧数（调试/指标）
    size_t pendingFrames() const;

    struct SpatialState {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int32_t dimensionId = 0;
        uint8_t envFlags = protocol::EnvFlagNone;
        int64_t updatedAtMs = 0;
        bool positionKnown = false;
    };

    // 网络线程更新，音频线程读取；与音频抖动状态共用同一把锁。
    bool updatePosition(const protocol::PosUpdateMessage& update, int64_t receivedAtMs);
    SpatialState spatialState() const;

private:
    bool allowFrameLocked(int64_t nowMs);

    protocol::PlayerId id_;
    Options options_;
    mutable std::mutex mutex_;

    std::deque<int64_t> arrivals_; // 限速滑动窗口（1s）
    audio::JitterBuffer jitter_;
    codec::OpusDecoder decoder_;
    SpatialState spatial_;
};

} // namespace bsc::server
