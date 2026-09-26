#include "server/session/PlayerSession.h"

#include <algorithm>

namespace bsc::server {

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

std::optional<PlayerSession::FrameOut> PlayerSession::pollFrame(int64_t nowMs) {
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

    return FrameOut{std::move(pcm), frame->flags};
}

void PlayerSession::reset() {
    std::lock_guard lock(mutex_);
    jitter_.clear();
    decoder_.reset();
    arrivals_.clear();
}

size_t PlayerSession::pendingFrames() const {
    std::lock_guard lock(mutex_);
    return jitter_.size();
}

bool PlayerSession::updatePosition(const protocol::PosUpdateMessage& update, int64_t receivedAtMs) {
    if (update.playerId != id_) return false;
    std::lock_guard lock(mutex_);
    if (spatial_.positionKnown && update.sendAtMs < static_cast<uint64_t>(spatial_.updatedAtMs)) return false;
    spatial_.x = update.x;
    spatial_.y = update.y;
    spatial_.z = update.z;
    spatial_.dimensionId = update.dimensionId;
    spatial_.envFlags = update.envFlags;
    spatial_.updatedAtMs = receivedAtMs;
    spatial_.positionKnown = true;
    return true;
}

PlayerSession::SpatialState PlayerSession::spatialState() const {
    std::lock_guard lock(mutex_);
    return spatial_;
}

bool PlayerSession::allowFrameLocked(int64_t nowMs) {
    arrivals_.push_back(nowMs);
    while (!arrivals_.empty() && arrivals_.front() <= nowMs - 1000) {
        arrivals_.pop_front();
    }
    return arrivals_.size() <= options_.maxFramesPerSecond;
}

} // namespace bsc::server
