#include "core/audio/SpatialPolicy.h"

#include <algorithm>
#include <cmath>

namespace bsc::audio {

SpatialPolicy::SpatialPolicy(SpatialPolicyConfig cfg) : cfg_(cfg) {}

float SpatialPolicy::gain(const SpatialGainQuery& q) const {
    // 跨维度隔离（Global 与 Proximity 均生效）
    if (cfg_.dimensionIsolation && !q.sameDimension) return 0.0f;

    if (cfg_.mode == MixMode::Global) {
        // Global：同维度即可闻，不吃距离、不看位置新鲜度。
        return 1.0f;
    }

    // --- Proximity 模式 ---
    if (!q.positionKnown) return 0.0f;            // 位置未知 → 听不到
    if (q.distBlocks < 0.0f) return 0.0f;         // 非法距离（防御）
    if (q.distBlocks >= cfg_.maxAudibleRadiusBlocks) return 0.0f; // 超可听上限
    if (cfg_.attenuationRadiusBlocks <= 0.0f) return 0.0f;        // 无有效衰减半径，避免除零 → 听不到

    // d=clamp(dist,0,maxAudible)，归一化到衰减半径并取上界 1
    const float t = std::min(q.distBlocks / cfg_.attenuationRadiusBlocks, 1.0f);
    // 衰减曲线：d=0 → 1；d>=r → 0；指数控制陡峭程度
    float base = std::pow(1.0f - t, cfg_.exponent);
    // 夹到 [minGain, 1]：近距保底可闻下限、上限不超过 1
    return std::clamp(std::max(base, cfg_.minGain), 0.0f, 1.0f);
}

} // namespace bsc::audio