#include "core/audio/SpatialPolicy.h"

#include <algorithm>
#include <cmath>

namespace bsc::audio {

SpatialPolicy::SpatialPolicy(SpatialPolicyConfig cfg) : cfg_(cfg) {}

float SpatialPolicy::gain(const SpatialGainQuery& q, MixMode mode) const {
    // 跨维度隔离（Global 与 Proximity 均生效）
    if (cfg_.dimensionIsolation && !q.sameDimension) return 0.0f;

    if (mode == MixMode::Global) {
        // Global：同维度即可闻，不吃距离、不看位置新鲜度。
        return 1.0f;
    }

    // --- Proximity 模式 ---
    if (!q.positionKnown) return 0.0f;   // 位置未知 → 听不到
    if (q.distBlocks < 0.0f) return 0.0f; // 非法距离（防御）
    if (cfg_.maxDistanceBlocks <= 0.0f) return 0.0f; // 未配置传播距离 → 不可听

    // 传播距离不大于衰减半径：退化成硬边界（半径内满增益，之外不可闻），
    // 既避免除零，也比“先判 d<=r 再判 d>=max”更符合配置意图。
    if (cfg_.maxDistanceBlocks <= cfg_.attenuationRadiusBlocks) {
        return q.distBlocks < cfg_.maxDistanceBlocks ? 1.0f : 0.0f;
    }

    // 衰减半径内满增益：近距离语音只在这之外开始随距离衰减。
    if (q.distBlocks <= cfg_.attenuationRadiusBlocks) return 1.0f;
    if (q.distBlocks >= cfg_.maxDistanceBlocks) return 0.0f;

    // 衰减段：t 从 0（半径处）到 1（最大传播距离处），曲线连续降到 0。
    const float span = cfg_.maxDistanceBlocks - cfg_.attenuationRadiusBlocks;
    const float t = (q.distBlocks - cfg_.attenuationRadiusBlocks) / span;
    const float base = std::pow(1.0f - std::clamp(t, 0.0f, 1.0f), std::max(0.0f, cfg_.exponent));
    return std::clamp(base, 0.0f, 1.0f);
}

} // namespace bsc::audio
