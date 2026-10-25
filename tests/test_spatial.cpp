#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/MixerCore.h"
#include "core/audio/SpatialPolicy.h"
#include "core/protocol/Message.h"

using namespace bsc::audio;
using namespace bsc::protocol;

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
    cfg.maxDistanceBlocks = 48.0f;
    cfg.dimensionIsolation = true;
    cfg.exponent = 1.0f;
    SpatialPolicy policy(cfg);

    SpatialGainQuery q;
    q.listener = makePlayerId(1);
    q.speaker = makePlayerId(2);
    q.sameDimension = true;
    q.positionKnown = true;

    // 衰减半径内：满增益 1，不做距离衰减
    q.distBlocks = 10.0f;
    EXPECT_NEAR(policy.gain(q), 1.0f, 1e-6);
    EXPECT_TRUE(policy.isAudible(q));

    // 零距离与正好在半径上：仍是满增益
    q.distBlocks = 0.0f;
    EXPECT_NEAR(policy.gain(q), 1.0f, 1e-6);
    q.distBlocks = 24.0f;
    EXPECT_NEAR(policy.gain(q), 1.0f, 1e-6);

    // 衰减段：r=24、max=48，d=36 → t=0.5 → 0.5
    q.distBlocks = 36.0f;
    EXPECT_NEAR(policy.gain(q), 0.5f, 1e-4);

    // 超最大传播距离：d >= max → 0
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

    // 指数 >1 更陡：衰减段中距增益应小于线性
    q.positionKnown = true;
    q.distBlocks = 36.0f;
    SpatialPolicyConfig steepCfg = cfg;
    steepCfg.exponent = 2.0f;
    SpatialPolicy steep(steepCfg);
    float gSteep = steep.gain(q);
    float gLinear = policy.gain(q);
    EXPECT_TRUE(gSteep >= 0.0f && gLinear >= 0.0f);
    EXPECT_TRUE(gSteep < gLinear);

    // 最大传播距离不大于衰减半径：退化为硬边界，仍可正确判定
    SpatialPolicyConfig hardEdge = cfg;
    hardEdge.attenuationRadiusBlocks = 30.0f;
    hardEdge.maxDistanceBlocks = 20.0f;
    SpatialPolicy hard(hardEdge);
    q.distBlocks = 10.0f;
    EXPECT_NEAR(hard.gain(q), 1.0f, 1e-6);
    q.distBlocks = 25.0f;
    EXPECT_NEAR(hard.gain(q), 0.0f, 1e-6);

    // 最大传播距离 <= 0（未配置）→ 不可闻，避免除零
    SpatialPolicyConfig noDistance = cfg;
    noDistance.maxDistanceBlocks = 0.0f;
    SpatialPolicy none(noDistance);
    q.distBlocks = 1.0f;
    EXPECT_NEAR(none.gain(q), 0.0f, 1e-6);
}

// 同一份策略可按会话切换模式：服务端开启近距离时，未协商到能力的旧会话
// 必须能降级为全局混音，而不是被距离直接判聋。
TEST(spatial_mode_can_be_chosen_per_call) {
    SpatialPolicyConfig cfg;
    cfg.mode = MixMode::Proximity;
    cfg.attenuationRadiusBlocks = 8.0f;
    cfg.maxDistanceBlocks = 16.0f;
    SpatialPolicy policy(cfg);

    SpatialGainQuery q;
    q.sameDimension = true;
    q.positionKnown = false;
    q.distBlocks = 999.0f;
    EXPECT_NEAR(policy.gain(q, MixMode::Proximity), 0.0f, 1e-6);
    EXPECT_NEAR(policy.gain(q, MixMode::Global), 1.0f, 1e-6);
}

TEST(mixer_core_mixes_two_speakers_by_gain) {
    MixerCore mixer(kFrameSamples, 48000);
    auto lA = makePlayerId(1); // 接收者 A
    auto sB = makePlayerId(2); // 说话者 B
    auto sC = makePlayerId(3); // 说话者 C

    // 两个说话者：正弦（不同频率），幅度 0.3 → 叠加峰值不超过软限幅阈值，
    // 这样可以逐样本断言“直接相加”的语义。
    auto tone = [](float freq, float phase) {
        std::vector<float> pcm(kFrameSamples);
        for (int i = 0; i < kFrameSamples; ++i) {
            pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * freq * i / 48000.0 + phase));
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

    // 单说话者：固定限幅阈值以下原样通过，输出 == B 的信号
    bool nonZero = false;
    for (float v : out) if (std::fabs(v) > 1e-4) { nonZero = true; break; }
    EXPECT_TRUE(nonZero);

    double eOut = 0, eB = 0;
    for (int i = 0; i < kFrameSamples; ++i) { eOut += out[i] * out[i]; eB += pcmB[i] * pcmB[i]; }
    EXPECT_TRUE(std::fabs(eOut - eB) < 1e-3 * eB);

    // A 同时被 B、C 听到：不再做 1/sqrt(N) 功率归一，两路直接相加；
    // 峰值超过软限幅阈值后才被压，阈值以下必须精确等于两路之和。
    mixer.setGain(lA, sC, 1.0f);
    std::vector<float> out2(kFrameSamples);
    mixer.computeMix(lA, out2.data());
    float peak2 = 0.0f;
    for (int i = 0; i < kFrameSamples; ++i) peak2 = std::max(peak2, std::fabs(out2[i]));
    EXPECT_TRUE(peak2 <= 1.0f + 1e-6); // 未削波
    for (int i = 0; i < kFrameSamples; ++i) {
        EXPECT_NEAR(out2[i], pcmB[i] + pcmC[i], 1e-5);
    }
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