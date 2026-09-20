#include "core/audio/MixerCore.h"

#include <algorithm>
#include <cmath>

namespace vc::audio {

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

    // 统计本接收者实际参与的非零增益说话者数
    size_t activeCount = 0;
    for (auto const& [speaker, gain] : lit->second) {
        if (gain > 0.0f && frames_.count(speaker)) ++activeCount;
    }
    if (activeCount == 0) return;

    // 功率归一：随参与说话者数降低单位增益，避免叠加削波（与 GlobalMixer 一致）
    const float norm = 1.0f / std::sqrt(static_cast<float>(activeCount));

    for (auto const& [speaker, gain] : lit->second) {
        if (gain <= 0.0f) continue;
        auto fit = frames_.find(speaker);
        if (fit == frames_.end()) continue;
        size_t n = std::min(fit->second.size(), frameSize);
        for (size_t i = 0; i < n; ++i) {
            out[i] += fit->second[i] * gain * norm;
        }
    }

    // 限幅保护
    for (size_t i = 0; i < frameSize; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
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

} // namespace vc::audio