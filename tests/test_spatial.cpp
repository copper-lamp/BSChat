#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/MixerCore.h"
#include "core/audio/SpatialPolicy.h"
#include "core/protocol/Message.h"

using namespace vc::audio;
using namespace vc::protocol;

namespace {

constexpr int kFrameSamples = 128;

PlayerId makePlayerId(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

} // namespace

TEST(spatial_global_same_dimension_gain_one) {
    SpatialPolicyConfig cfg;
    cfg.mode = MixMode::Global;
    cfg.dimensionIsolation = true;
    SpatialPolicy policy(cfg);

    SpatialGainQuery q;
    q.listener = makePlayerId(1);
    q.speaker = makePlayerId(2);
    q.distBlocks = 1000.0f;      // Global 不吃距离
    q.sameDimension = true;
    q.positionKnown = false;     // Global 不看位置新鲜度
    EXPECT_NEAR(policy.gain(q), 1.0f, 1e-6);
    EXPECT_TRUE(policy.isAudible(q));

    // 跨维度 → 隔离为 0
    q.sameDimension = false;
    EXPECT_NEAR(policy.gain(q), 0.0f, 1e-6);
    EXPECT_FALSE(policy.isAudible(q));

    // 关闭维度隔离后，跨维度 Global 仍可闻
    cfg.dimensionIsolation = false;
    SpatialPolicy noIsolation(cfg);
    q.sameDimension = false;
    EXPECT_NEAR(noIsolation.gain(q), 1.0f, 1e-6);
    EXPECT_TRUE(noIsolation.isAudible(q));
}

TEST(spatial_proximity_curve_and_gates) {
    SpatialPolicyConfig cfg;
    cfg.mode = MixMode::Proximity;
    cfg.attenuationRadiusBlocks = 24.0f;
    cfg.maxAudibleRadiusBlocks = 48.0f;
    cfg.dimensionIsolation = true;
    cfg.exponent = 1.0f;
    SpatialPolicy policy(cfg);

    SpatialGainQuery q;
    q.listener = makePlayerId(1);
    q.speaker = makePlayerId(2);
    q.sameDimension = true;
    q.positionKnown = true;

    // 近距：d(=10) < r(=24) → 线性衰减 gain = 1 - 10/24 > 0 且较大
    q.distBlocks = 10.0f;
    float gNear = policy.gain(q);
    EXPECT_TRUE(gNear > 0.0f);
    EXPECT_NEAR(gNear, 1.0f - (10.0f / 24.0f), 1e-4);
    EXPECT_TRUE(policy.isAudible(q));

    // 零距离 → 最大增益 1
    q.distBlocks = 0.0f;
    EXPECT_NEAR(policy.gain(q), 1.0f, 1e-6);

    // 衰减半径处及其后：(d>=r) 但仍 < maxAudible，base=0 → 夹到 minGain
    q.distBlocks = 24.0f;
    EXPECT_NEAR(policy.gain(q), cfg.minGain, 1e-6);

    // 超可听上限：d >= maxAudible(48) → 0
    q.distBlocks = 48.0f;
    EXPECT_NEAR(policy.gain(q), 0.0f, 1e-6);
    q.distBlocks = 60.0f;
    EXPECT_NEAR(policy.gain(q), 0.0f, 1e-6);
    EXPECT_FALSE(policy.isAudible(q));

    // 跨维度 → 0
    q.distBlocks = 10.0f;
    q.sameDimension = false;
    EXPECT_NEAR(policy.gain(q), 0.0f, 1e-6);

    // 未知位置 → 0
    q.sameDimension = true;
    q.positionKnown = false;
    EXPECT_NEAR(policy.gain(q), 0.0f, 1e-6);

    // 衰减半径 <=0 → 避免除零，返回 0
    SpatialPolicyConfig badRadius = cfg;
    badRadius.attenuationRadiusBlocks = 0.0f;
    SpatialPolicy badPolicy(badRadius);
    q.positionKnown = true;
    q.distBlocks = 5.0f;
    EXPECT_NEAR(badPolicy.gain(q), 0.0f, 1e-6);

    // 指数 >1 更陡：中距增益应小于线性
    q.distBlocks = 12.0f;
    SpatialPolicyConfig steepCfg = cfg;
    steepCfg.exponent = 2.0f;
    SpatialPolicy steep(steepCfg);
    float gSteep = steep.gain(q);
    float gLinear = policy.gain(q);
    EXPECT_TRUE(gSteep >= 0.0f && gLinear >= 0.0f);
    EXPECT_TRUE(gSteep < gLinear);
}

TEST(mixer_core_mixes_two_speakers_by_gain) {
    MixerCore mixer(kFrameSamples, 48000);
    auto lA = makePlayerId(1); // 接收者 A
    auto sB = makePlayerId(2); // 说话者 B
    auto sC = makePlayerId(3); // 说话者 C

    // 两个说话者：正弦（不同频率），能量恒定为 0.5^2
    auto tone = [](float freq, float phase) {
        std::vector<float> pcm(kFrameSamples);
        for (int i = 0; i < kFrameSamples; ++i) {
            pcm[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * freq * i / 48000.0 + phase));
        }
        return pcm;
    };
    auto pcmB = tone(440.0f, 0.0f);
    auto pcmC = tone(880.0f, 1.0f);

    mixer.addSpeakerFrame(sB, pcmB);
    mixer.addSpeakerFrame(sC, pcmC);
    EXPECT_TRUE(mixer.hasActiveTalker());

    // A 只听 B（增益 1.0），听不到 C（缺省 0）
    mixer.setGain(lA, sB, 1.0f);

    std::vector<float> out(kFrameSamples);
    mixer.computeMix(lA, out.data());

    // 单说话者 activeCount=1 → 归一增益 1，输出 == B 的信号
    bool nonZero = false;
    for (float v : out) if (std::fabs(v) > 1e-4) { nonZero = true; break; }
    EXPECT_TRUE(nonZero);

    // 功率对齐：out 能量应与单路 0.5^2 正弦一致
    double eOut = 0, eB = 0;
    for (int i = 0; i < kFrameSamples; ++i) { eOut += out[i] * out[i]; eB += pcmB[i] * pcmB[i]; }
    EXPECT_TRUE(std::fabs(eOut - eB) < 1e-3 * eB);

    // A 同时被 B、C 听到：activeCount=2 → 归一 1/sqrt2，两路叠加
    // 非正交双音叠加存在交叉项，能量不严格可加，故用宽松区间 + 峰值断言。
    mixer.setGain(lA, sC, 1.0f);
    std::vector<float> out2(kFrameSamples);
    mixer.computeMix(lA, out2.data());
    double eOut2 = 0;
    float peak2 = 0.0f;
    for (int i = 0; i < kFrameSamples; ++i) {
        eOut2 += out2[i] * out2[i];
        peak2 = std::max(peak2, std::fabs(out2[i]));
    }
    EXPECT_TRUE(eOut2 > 0.4 * eB && eOut2 < 1.6 * eB); // 能量与单路同一量级
    EXPECT_TRUE(peak2 <= 1.0f + 1e-6);                 // 未削波
    EXPECT_NEAR(std::fabs(out2[0]), std::fabs((pcmB[0] + pcmC[0]) / std::sqrt(2.0f)), 1e-6);
}

TEST(mixer_core_clear_gain_zeroes_output) {
    MixerCore mixer(kFrameSamples, 48000);
    auto lA = makePlayerId(1);
    auto sB = makePlayerId(2);

    auto pcmB = std::vector<float>(kFrameSamples, 0.3f);
    mixer.addSpeakerFrame(sB, pcmB);
    mixer.setGain(lA, sB, 1.0f);

    std::vector<float> out(kFrameSamples);
    mixer.computeMix(lA, out.data());
    EXPECT_NEAR(out[0], 0.3f, 1e-6);

    // 清增益 → 输出全 0
    mixer.clearGain(lA);
    mixer.computeMix(lA, out.data());
    bool allZero = true;
    for (float v : out) if (std::fabs(v) > 1e-7) { allZero = false; break; }
    EXPECT_TRUE(allZero);
}

TEST(mixer_core_clamps_peak_to_unit) {
    MixerCore mixer(kFrameSamples, 48000);
    auto lA = makePlayerId(1);
    auto sB = makePlayerId(2);

    // 放大增益使叠加后可能削波
    auto pcmB = std::vector<float>(kFrameSamples, 0.9f);
    mixer.addSpeakerFrame(sB, pcmB);
    mixer.setGain(lA, sB, 5.0f);

    std::vector<float> out(kFrameSamples);
    mixer.computeMix(lA, out.data());
    for (float v : out) {
        EXPECT_TRUE(v >= -1.0f - 1e-6 && v <= 1.0f + 1e-6);
    }
}

TEST(mixer_core_reset_clears_state) {
    MixerCore mixer(kFrameSamples, 48000);
    auto lA = makePlayerId(1);
    auto sB = makePlayerId(2);
    mixer.addSpeakerFrame(sB, std::vector<float>(kFrameSamples, 0.5f));
    mixer.setGain(lA, sB, 1.0f);
    EXPECT_TRUE(mixer.hasActiveTalker());

    mixer.reset();
    EXPECT_FALSE(mixer.hasActiveTalker());

    std::vector<float> out(kFrameSamples, 42.0f);
    mixer.computeMix(lA, out.data());
    bool allZero = true;
    for (float v : out) if (std::fabs(v) > 1e-7) { allZero = false; break; }
    EXPECT_TRUE(allZero);
}