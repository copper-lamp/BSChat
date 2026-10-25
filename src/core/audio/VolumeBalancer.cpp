#include "core/audio/VolumeBalancer.h"

#include <algorithm>
#include <cmath>

namespace bsc::audio {
namespace {

// 压小比抬大更快：前者是防爆音，后者是响度补偿，慢一点更自然。
constexpr float kAttackMs = 30.0f;
constexpr float kReleaseMs = 400.0f;

float timeCoefficient(int frameSizeMs, float tauMs) {
    if (tauMs <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-static_cast<float>(frameSizeMs) / tauMs);
}

} // namespace

VolumeBalancer::VolumeBalancer(int frameSizeMs) : frameSizeMs_(std::max(1, frameSizeMs)) {}

float VolumeBalancer::smooth(float current, float desired) const {
    // 目标增益小于当前值 = 需要压小 → attack；否则是释放 → release。
    const float tau = desired < current ? kAttackMs : kReleaseMs;
    const float coeff = timeCoefficient(frameSizeMs_, tau);
    return current + (desired - current) * coeff;
}

float VolumeBalancer::gainFor(
    const protocol::PlayerId& speaker,
    float frameRms,
    const VolumeBalanceSettings& settings
) {
    auto& state = states_[speaker];

    if (!settings.enabled || settings.strength <= 0.0f) {
        // 关闭时把状态归一到 1，重新打开后从「不改变音量」开始平滑。
        state.gain = 1.0f;
        return 1.0f;
    }

    // 静音帧（停顿、DTX 空帧）不参与测量：否则噪声底会把增益一路推到上限，
    // 下一句话一开口就是爆音。非法参数同样按「不处理」降级，绝不让 NaN 进入混音。
    if (!std::isfinite(frameRms) || frameRms < kSilenceRms || !std::isfinite(settings.targetRms)) {
        return state.gain;
    }

    const float target = std::clamp(settings.targetRms, protocol::kVolumeBalanceTargetMin,
                                    protocol::kVolumeBalanceTargetMax);
    const float desiredRaw = std::clamp(target / frameRms, kMinGain, kMaxGain);
    const float strength = std::clamp(settings.strength, protocol::kVolumeBalanceStrengthMin,
                                      protocol::kVolumeBalanceStrengthMax);
    // 强度只在「目标增益」与「保持原样」之间插值，0 等价于关闭。
    const float desired = 1.0f + strength * (desiredRaw - 1.0f);

    state.gain = std::clamp(smooth(state.gain, desired), kMinGain, kMaxGain);
    return state.gain;
}

void VolumeBalancer::forget(const protocol::PlayerId& speaker) {
    states_.erase(speaker);
}

void VolumeBalancer::reset() {
    states_.clear();
}

} // namespace bsc::audio
