#include "core/audio/GlobalMixer.h"

#include <algorithm>
#include <cmath>

namespace bsc::audio {

GlobalMixer::GlobalMixer(Config config) : config_(config) {}

void GlobalMixer::addFrame(const protocol::PlayerId& speaker, std::vector<float> pcm) {
    auto& buf = speakers_[speaker].data;
    buf.insert(buf.end(), pcm.begin(), pcm.end());
}

bool GlobalMixer::hasActiveTalker() const {
    for (auto const& [id, buf] : speakers_) {
        if (!buf.data.empty()) return true;
    }
    return false;
}

void GlobalMixer::mix(float* out) {
    const size_t frameSize = static_cast<size_t>(config_.frameSamples);
    std::fill(out, out + frameSize, 0.0f);

    size_t activeCount = 0;
    for (auto const& [id, buf] : speakers_) {
        if (!buf.data.empty()) ++activeCount;
    }
    if (activeCount == 0) return;

    // 功率归一：随活跃数增加降低每路增益，避免叠加削波
    const float gain = 1.0f / std::sqrt(static_cast<float>(activeCount));

    for (auto& [id, buf] : speakers_) {
        if (buf.data.empty()) continue;
        size_t n = std::min(buf.data.size(), frameSize);
        for (size_t i = 0; i < n; ++i) {
            out[i] += buf.data[i] * gain;
        }
        buf.data.erase(buf.data.begin(), buf.data.begin() + static_cast<std::ptrdiff_t>(n));
    }

    // 限幅保护
    for (size_t i = 0; i < frameSize; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
}

void GlobalMixer::removeSpeaker(const protocol::PlayerId& speaker) {
    speakers_.erase(speaker);
}

} // namespace bsc::audio
