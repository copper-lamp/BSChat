#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace bsc::audio {

// 抖动缓冲：按发送序号排序，吸收网络抖动并容忍乱序/丢失。
// 规则：
//  - 乱序包（seq < 期望序号）直接丢弃；
//  - 期望帧未到但队首已等待超过 maxWaitMs 时放行队首（容忍丢失）；
//  - 缓冲深度超上限时丢弃最旧帧。
// 时间由调用方注入（nowMs），便于单测。
class JitterBuffer {
public:
    struct Options {
        size_t maxDepthFrames = 6;  // 6 × 60ms = 360ms 深度上限
        int64_t maxWaitMs = 200;    // 队首等待超时后放行
    };

    // 出帧结果：数据 + 随帧标志（如 AudioFlag Start/End，用于话语切分）
    struct Frame {
        std::vector<uint8_t> data;
        uint8_t flags = 0;
    };

    enum class PushResult {
        Accepted,
        AcceptedWithEviction,
        Late,
        Duplicate,
        BufferFull
    };

    JitterBuffer();
    explicit JitterBuffer(Options options);

    void push(uint64_t seq, std::vector<uint8_t> payload, int64_t arrivalMs, uint8_t flags = 0);

    // 取下一帧；无可放行帧返回 nullopt
    std::optional<Frame> pop(int64_t nowMs);

    void clear();
    size_t size() const;
    uint64_t nextExpectedSeq() const { return nextExpectedSeq_; }

private:
    Options options_;
    uint64_t nextExpectedSeq_ = 0;
    bool started_ = false; // 收到首帧后开始跟踪期望序号
    // 帧结构：payload + 到达时间 + 标志
    struct Entry {
        std::vector<uint8_t> payload;
        int64_t arrivalMs = 0;
        uint8_t flags = 0;
    };
    std::map<uint64_t, Entry> frames_;
};

} // namespace bsc::audio
