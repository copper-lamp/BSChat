#include "core/audio/JitterBuffer.h"

namespace vc::audio {

JitterBuffer::JitterBuffer() : JitterBuffer(Options{}) {}

JitterBuffer::JitterBuffer(Options options) : options_(options) {}

void JitterBuffer::push(uint64_t seq, std::vector<uint8_t> payload, int64_t arrivalMs) {
    if (!started_) {
        started_ = true;
        nextExpectedSeq_ = seq;
    }
    if (seq < nextExpectedSeq_) return; // 迟到帧 → 丢弃
    if (frames_.size() >= options_.maxDepthFrames) {
        uint64_t evictedSeq = frames_.begin()->first;
        frames_.erase(frames_.begin()); // 超深 → 丢最旧
        // 被驱逐的队首即为等待目标（或更旧的缺口），直接推进期望，避免永久卡顿
        nextExpectedSeq_ = evictedSeq + 1;
    }
    frames_.emplace(seq, Entry{std::move(payload), arrivalMs});
}

std::optional<std::vector<uint8_t>> JitterBuffer::pop(int64_t nowMs) {
    if (frames_.empty()) return std::nullopt;

    auto firstIt = frames_.begin();
    if (firstIt->first == nextExpectedSeq_) {
        auto out = std::move(firstIt->second.payload);
        frames_.erase(firstIt);
        ++nextExpectedSeq_;
        return out;
    }
    // 期望帧缺失：若队首等待超时则放行队首（跳过缺口）
    if (nowMs - firstIt->second.arrivalMs >= options_.maxWaitMs) {
        auto out = std::move(firstIt->second.payload);
        uint64_t seq = firstIt->first;
        frames_.erase(firstIt);
        nextExpectedSeq_ = seq + 1;
        return out;
    }
    return std::nullopt;
}

void JitterBuffer::clear() {
    frames_.clear();
    started_ = false;
    nextExpectedSeq_ = 0;
}

size_t JitterBuffer::size() const { return frames_.size(); }

} // namespace vc::audio
