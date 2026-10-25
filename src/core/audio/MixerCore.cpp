#include "core/audio/MixerCore.h"

#include <algorithm>
#include <cmath>

namespace bsc::audio {
namespace {

// 固定裕量软限幅：阈值以下原样通过；阈值以上用 tanh 把剩余动态压进
// (threshold, 1.0] 区间。曲线在阈值处一阶连续（tanh(0)=0、导数=1），
// 不会像硬削波那样在波形上留下折点。
float softLimit(float sample) {
    constexpr float kThreshold = MixerCore::kSoftLimitThreshold;
    const float magnitude = std::fabs(sample);
    if (magnitude <= kThreshold) return sample;
    const float headroom = 1.0f - kThreshold;
    const float limited = kThreshold + headroom * std::tanh((magnitude - kThreshold) / headroom);
    return std::copysign(std::min(limited, 1.0f), sample);
}

} // namespace

MixerCore::MixerCore(int frameSamples, int sampleRate)
    : frameSamples_(frameSamples), sampleRate_(sampleRate) {}

void MixerCore::addSpeakerFrame(protocol::PlayerId speaker, FloatVector pcm) {
    frames_[speaker] = std::move(pcm); // 覆盖语义：保留本帧最新数据
}

void MixerCore::setGain(protocol::PlayerId listener, protocol::PlayerId speaker, float gain) {
    gains_[listener][speaker] = std::max(0.0f, gain); // 负增益视为 0（听不到）
}

void MixerCore::clearGain(protocol::PlayerId listener) {
    gains_.erase(listener);
}

void MixerCore::computeMix(protocol::PlayerId listener, float* out) const {
    const size_t frameSize = static_cast<size_t>(frameSamples_);
    std::fill(out, out + frameSize, 0.0f);

    auto lit = gains_.find(listener);
    if (lit == gains_.end()) return; // 无增益配置 → 全 0

    for (auto const& [speaker, gain] : lit->second) {
        if (gain <= 0.0f) continue;
        auto fit = frames_.find(speaker);
        if (fit == frames_.end()) continue;
        size_t n = std::min(fit->second.size(), frameSize);
        for (size_t i = 0; i < n; ++i) {
            out[i] += fit->second[i] * gain;
        }
    }

    // 固定裕量限幅保护
    for (size_t i = 0; i < frameSize; ++i) {
        out[i] = softLimit(out[i]);
    }
}

bool MixerCore::hasActiveTalker() const {
    return !frames_.empty();
}

void MixerCore::clearFrames() {
    frames_.clear();
}

void MixerCore::reset() {
    frames_.clear();
    gains_.clear();
}

} // namespace bsc::audio
