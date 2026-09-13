#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/audio/JitterBuffer.h"
#include "core/codec/OpusCodec.h"
#include "core/protocol/Message.h"

namespace vc::server {

// 单个玩家的服务端会话：每发送者一个 Opus 解码器 + 抖动缓冲 + PTT 话语切分 + 限速。
// 零 LeviLamina 依赖，可纯 host 单测。
//
// 线程模型：
//  - pushAudio()    ：网络线程（包到达），互斥锁保护，仅入抖动缓冲与限速窗口；
//  - pollFrame()    ：音频线程，取一帧解码 PCM（空 = 静音/无帧），内部完成 PTT 切分；
//  - pollUtterance()：音频线程，取一段完成话语 PCM（供 STT 提交）；
// 解码器仅由音频线程使用，天然单线程。
class PlayerSession {
public:
    struct Options {
        int sampleRate = audio::kDefaultSampleRate;
        int channels = audio::kDefaultChannels;
        int frameSizeMs = audio::kDefaultFrameSizeMs;
        size_t maxFramesPerSecond = 40; // 客户端 ~16.7fps（60ms/帧），宽松上限防刷包
        size_t maxUtteranceMs = 30000;  // 单句上限，超长强制切分
        size_t maxDepthFrames = 6;      // 抖动缓冲深度（帧）
        int64_t maxWaitMs = 200;        // 抖动缓冲队首等待（ms）
    };

    explicit PlayerSession(protocol::PlayerId id, Options options);
    ~PlayerSession();
    PlayerSession(const PlayerSession&) = delete;
    PlayerSession& operator=(const PlayerSession&) = delete;

    const protocol::PlayerId& id() const { return id_; }
    const Options& options() const { return options_; }

    // 网络线程：接收一帧上行语音（seq/flags/opusData）；nowMs 为稳态时钟毫秒。
    void pushAudio(const protocol::AudioDataMessage& msg, int64_t nowMs);

    // 音频线程：取下一帧解码后 PCM（float，[-1,1]）；无帧返回 nullopt。
    // 返回的 vector 为空表示该帧为静音（无数据），混音按静音处理。
    std::optional<std::vector<float>> pollFrame(int64_t nowMs);

    // 音频线程：取一段完成话语（PTT 切分产物），供 STT 提交；无则返回 nullopt。
    std::optional<std::vector<float>> pollUtterance();

    // 会话复位（重连/参数变更）：清抖动缓冲、解码器状态与未完成话语。
    void reset();

    // 缓冲中待处理帧数（调试/指标）
    size_t pendingFrames() const;

private:
    void finalizeUtteranceLocked();
    bool allowFrameLocked(int64_t nowMs);

    protocol::PlayerId id_;
    Options options_;
    mutable std::mutex mutex_;

    std::deque<int64_t> arrivals_; // 限速滑动窗口（1s）
    audio::JitterBuffer jitter_;
    codec::OpusDecoder decoder_;

    bool inUtterance_ = false;
    std::vector<float> utterancePcm_;         // 当前话语累积 PCM
    size_t utteranceSamples_ = 0;             // 已累积样本数（超长切分）
    std::deque<std::vector<float>> utterances_; // 完成话语队列
};

} // namespace vc::server
