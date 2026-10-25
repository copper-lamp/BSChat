#pragma once

#include "core/protocol/Message.h"

namespace bsc::audio {

// 空间选路模式
enum class MixMode { Global, Proximity };

struct SpatialPolicyConfig {
    MixMode mode = MixMode::Global;
    // 衰减半径（方块）：d <= attenuationRadius 时满增益 1。
    float attenuationRadiusBlocks = 24.0f;
    // 最大传播距离（方块）：attenuationRadius < d < maxDistance 之间按下述曲线衰减，
    // d >= maxDistance 直接不可闻（增益 0，字幕也不再发送）。
    float maxDistanceBlocks = 48.0f;
    bool dimensionIsolation = true;        // 跨维度隔离：非同一维度 → gain=0
    float exponent = 1.0f;                 // 衰减段曲线指数（1=线性，>1 更陡）
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

    // 计算 gain。mode 由调用方按「本会话是否协商到近距离能力」选择，因此服务器的
    // 配置模式可以被旧客户端按会话降级为 Global，而不必为每人维护一份策略副本。
    //   Global     —— 仅做维度隔离（按 dimensionIsolation），同维度返回 1.0f；
    //                 不吃距离，positionKnown 不影响。
    //   Proximity  —— 跨维度 或 位置未知 一律 0；d<=衰减半径返回 1；
    //                 d>=最大传播距离返回 0；中间按 (1-t)^exponent 衰减。
    float gain(const SpatialGainQuery& q, MixMode mode) const;

    // 按配置默认模式计算（单测/不区分会话的调用方使用）。
    float gain(const SpatialGainQuery& q) const { return gain(q, cfg_.mode); }

    // 捷径：gain()>0。
    bool isAudible(const SpatialGainQuery& q, MixMode mode) const { return gain(q, mode) > 0.0f; }
    bool isAudible(const SpatialGainQuery& q) const { return gain(q) > 0.0f; }

    const SpatialPolicyConfig& config() const { return cfg_; }

private:
    SpatialPolicyConfig cfg_;
};

} // namespace bsc::audio
