#pragma once

#include "core/protocol/Message.h"

namespace vc::audio {

// 空间选路模式
enum class MixMode { Global, Proximity };

struct SpatialPolicyConfig {
    MixMode mode = MixMode::Global;
    float attenuationRadiusBlocks = 24.0f; // Proximity 衰减半径（方块）：d>=r 时视为衰减完
    float maxAudibleRadiusBlocks = 48.0f;  // 可听上限（方块）：d>=maxAudible → gain=0
    bool dimensionIsolation = true;        // 跨维度隔离：非同一维度 → gain=0
    float minGain = 0.01f;                 // Proximity 最小可闻下限（防断音抖动）
    float exponent = 1.0f;                 // 衰减曲线指数（1=线性，>1 更陡）
};

// 单次选路查询输入
struct SpatialGainQuery {
    protocol::PlayerId listener;
    protocol::PlayerId speaker;
    float distBlocks = 0.0f;      // 说话者相对接收者距离（方块）
    bool sameDimension = true;    // 是否同维度
    bool positionKnown = true;    // 说话者位置是否新鲜（新旧判定由上层完成）
};

// 空间选路策略：输入空间状态，输出某说话者对某接收者的增益（0=听不到）。
class SpatialPolicy {
public:
    explicit SpatialPolicy(SpatialPolicyConfig cfg);

    // 计算 gain。语义：
    //   Global     —— 仅做维度隔离（按 dimensionIsolation），同维度返回 1.0f；
    //                 不吃距离，positionKnown 不影响（未知位置是否纳入由上层决策）。
    //   Proximity  —— 跨维度 或 位置未知 一律 0；d>=maxAudible 或 衰减半径<=0 也 0；
    //                 否则 g = pow(1 - min(d/r,1), exponent)，再夹到 [minGain,1]。
    float gain(const SpatialGainQuery& q) const;

    // 捷径：gain()>0。
    bool isAudible(const SpatialGainQuery& q) const { return gain(q) > 0.0f; }

    const SpatialPolicyConfig& config() const { return cfg_; }

private:
    SpatialPolicyConfig cfg_;
};

} // namespace vc::audio