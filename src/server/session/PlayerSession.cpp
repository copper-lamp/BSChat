#include "server/session/PlayerSession.h"

#include <algorithm>

namespace vc::server {

PlayerSession::PlayerSession(protocol::PlayerId id, Options options)
: id_(id),
  options_(options),
  jitter_({options_.maxDepthFrames, options_.maxWaitMs}),
  decoder_(options_.sampleRate, options_.channels,
           audio::samplesPerFrame(options_.sampleRate, options_.frameSizeMs)) {}

PlayerSession::~PlayerSession() = default;

void PlayerSession::pushAudio(const protocol::AudioDataMessage& msg, int64_t nowMs) {
    std::lock_guard lock(mutex_);
    if (!allowFrameLocked(nowMs)) return; // 超限 → 整帧丢弃（限速）

    // 空数据帧（静音/仅标志）也要入抖动缓冲，保证 Start/End 标志按序生效
    jitter_.push(msg.seq, msg.opusData, nowMs, msg.flags);
}

std::optional<std::vector<float>> PlayerSession::pollFrame(int64_t nowMs) {
    std::lock_guard lock(mutex_);
    auto frame = jitter_.pop(nowMs);
    if (!frame) return std::nullopt;

    const int frameSamples = audio::samplesPerFrame(options_.sampleRate, options_.frameSizeMs);
    std::vector<float> pcm;
    if (!frame->data.empty()) {
        pcm.resize(static_cast<size_t>(frameSamples));
        int n = decoder_.decode(frame->data.data(), frame->data.size(), pcm.data(), pcm.size());
        if (n < 0) {
            pcm.clear(); // 解码失败 → 按静音处理，不影响链路
        } else {
            pcm.resize(static_cast<size_t>(n));
        }
    }

    // PTT 话语切分：Start 开新句，End 收尾；无标志帧仅在句内时累积
    if (frame->flags & protocol::AudioFlagStart) {
        inUtterance_ = false; // 丢弃上一句未完成的半截
        utterancePcm_.clear();
        utteranceSamples_ = 0;
        inUtterance_ = true;
    }
    if (inUtterance_ && !pcm.empty()) {
        utterancePcm_.insert(utterancePcm_.end(), pcm.begin(), pcm.end());
        utteranceSamples_ += pcm.size();
        const size_t maxSamples =
            static_cast<size_t>(options_.sampleRate) * options_.maxUtteranceMs / 1000;
        if (utteranceSamples_ >= maxSamples) {
            finalizeUtteranceLocked(); // 超长 → 强制切分
            inUtterance_ = true;       // 切分后继续累积剩余语音，不丢句尾
        }
    }
    if (frame->flags & protocol::AudioFlagEnd) {
        finalizeUtteranceLocked();
    }

    return pcm;
}

std::optional<std::vector<float>> PlayerSession::pollUtterance() {
    std::lock_guard lock(mutex_);
    if (utterances_.empty()) return std::nullopt;
    auto out = std::move(utterances_.front());
    utterances_.pop_front();
    return out;
}

void PlayerSession::reset() {
    std::lock_guard lock(mutex_);
    jitter_.clear();
    decoder_.reset();
    arrivals_.clear();
    inUtterance_ = false;
    utterancePcm_.clear();
    utteranceSamples_ = 0;
    utterances_.clear();
}

size_t PlayerSession::pendingFrames() const {
    std::lock_guard lock(mutex_);
    return jitter_.size();
}

void PlayerSession::finalizeUtteranceLocked() {
    if (!inUtterance_) return;
    if (!utterancePcm_.empty()) {
        utterances_.push_back(std::move(utterancePcm_));
    }
    utterancePcm_.clear();
    utteranceSamples_ = 0;
    inUtterance_ = false;
}

bool PlayerSession::allowFrameLocked(int64_t nowMs) {
    arrivals_.push_back(nowMs);
    while (!arrivals_.empty() && arrivals_.front() <= nowMs - 1000) {
        arrivals_.pop_front();
    }
    return arrivals_.size() <= options_.maxFramesPerSecond;
}

} // namespace vc::server
