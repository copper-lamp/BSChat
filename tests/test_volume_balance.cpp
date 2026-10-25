#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/MixerCore.h"
#include "core/audio/VolumeBalancer.h"
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

VolumeBalanceSettings enabledSettings(float strength = 1.0f) {
    VolumeBalanceSettings settings;
    settings.enabled = true;
    settings.strength = strength;
    settings.targetRms = 0.12f;
    return settings;
}

// 连续喂 n 帧固定 RMS，返回最终增益（20ms 一帧，平滑系数按帧长推进）。
float runFrames(VolumeBalancer& balancer, const PlayerId& speaker, float rms, int frames,
                const VolumeBalanceSettings& settings) {
    float gain = 1.0f;
    for (int i = 0; i < frames; ++i) gain = balancer.gainFor(speaker, rms, settings);
    return gain;
}

} // namespace

TEST(volume_balancer_disabled_is_pass_through) {
    VolumeBalancer balancer;
    VolumeBalanceSettings disabled;
    disabled.enabled = false;
    EXPECT_NEAR(balancer.gainFor(makePlayerId(1), 0.02f, disabled), 1.0f, 1e-6);
    EXPECT_NEAR(balancer.gainFor(makePlayerId(1), 0.9f, disabled), 1.0f, 1e-6);
}

TEST(volume_balancer_boosts_quiet_speaker_towards_target) {
    VolumeBalancer balancer;
    // RMS 0.03，目标 0.12 → 期望增益 4.0；抬升走慢释放，2 秒后应基本到位。
    const float gain = runFrames(balancer, makePlayerId(1), 0.03f, 100, enabledSettings());
    EXPECT_NEAR(gain, 4.0f, 0.1f);
}

TEST(volume_balancer_attenuates_loud_speaker_towards_target) {
    VolumeBalancer balancer;
    // RMS 0.5，目标 0.12 → 期望增益 0.24；压小走快 attack，400ms 内应接近。
    const float gain = runFrames(balancer, makePlayerId(2), 0.5f, 20, enabledSettings());
    EXPECT_NEAR(gain, 0.24f, 0.02f);
}

TEST(volume_balancer_strength_blends_towards_unchanged) {
    VolumeBalancer balancer;
    // 强度 0.5：目标增益 4 → 实际目标 1 + 0.5*(4-1) = 2.5
    const float gain = runFrames(balancer, makePlayerId(3), 0.03f, 100, enabledSettings(0.5f));
    EXPECT_NEAR(gain, 2.5f, 0.1f);

    // 强度 0 与关闭等价
    VolumeBalanceSettings zero = enabledSettings(0.0f);
    EXPECT_NEAR(balancer.gainFor(makePlayerId(3), 0.03f, zero), 1.0f, 1e-6);
}

TEST(volume_balancer_gain_is_bounded) {
    VolumeBalancer balancer;
    // 极小音量不能无限抬：上限 5 倍（~+14dB），避免把底噪/键盘声放大成语音。
    const float gain = runFrames(balancer, makePlayerId(4), 0.001f, 200, enabledSettings());
    EXPECT_TRUE(gain <= VolumeBalancer::kMaxGain + 1e-6f);
    EXPECT_TRUE(gain >= VolumeBalancer::kMinGain - 1e-6f);
}

TEST(volume_balancer_silence_does_not_move_gain) {
    VolumeBalancer balancer;
    runFrames(balancer, makePlayerId(5), 0.03f, 100, enabledSettings());
    const float before = balancer.gainFor(makePlayerId(5), 0.03f, enabledSettings());
    const float afterSilence = balancer.gainFor(makePlayerId(5), 0.0f, enabledSettings());
    EXPECT_NEAR(afterSilence, before, 1e-6);
}

TEST(volume_balancer_tracks_each_speaker_independently) {
    VolumeBalancer balancer;
    const auto quiet = makePlayerId(6);
    const auto loud = makePlayerId(7);
    runFrames(balancer, quiet, 0.03f, 100, enabledSettings());
    runFrames(balancer, loud, 0.5f, 100, enabledSettings());
    EXPECT_EQ(balancer.trackedSpeakers(), static_cast<std::size_t>(2));
    EXPECT_TRUE(balancer.gainFor(quiet, 0.03f, enabledSettings()) > 1.0f);
    EXPECT_TRUE(balancer.gainFor(loud, 0.5f, enabledSettings()) < 1.0f);

    balancer.forget(quiet);
    EXPECT_EQ(balancer.trackedSpeakers(), static_cast<std::size_t>(1));
    balancer.reset();
    EXPECT_EQ(balancer.trackedSpeakers(), static_cast<std::size_t>(0));
}

// 固定阈值软限幅：阈值以下不改变样本，超阈值只压缩不削波（硬削波会在 1.0 处平顶）。
TEST(mixer_core_soft_limiter_preserves_headroom) {
    MixerCore mixer(kFrameSamples, 48000);
    const auto listener = makePlayerId(10);

    // 三路 0.6 叠加 = 1.8，超过阈值 0.7 → 压到 1.0 以内且不出现硬平顶。
    for (uint8_t i = 0; i < 3; ++i) {
        mixer.addSpeakerFrame(makePlayerId(static_cast<uint8_t>(11 + i)),
                              std::vector<float>(kFrameSamples, 0.6f));
        mixer.setGain(listener, makePlayerId(static_cast<uint8_t>(11 + i)), 1.0f);
    }
    std::vector<float> out(kFrameSamples);
    mixer.computeMix(listener, out.data());
    for (float v : out) {
        EXPECT_TRUE(v <= 1.0f + 1e-6f);
        EXPECT_TRUE(v > 0.9f); // 软限幅后仍接近满幅，而不是被整体功率归一压低
    }

    // 阈值以下必须原样通过
    MixerCore quiet(kFrameSamples, 48000);
    quiet.addSpeakerFrame(makePlayerId(20), std::vector<float>(kFrameSamples, 0.5f));
    quiet.setGain(listener, makePlayerId(20), 1.0f);
    quiet.computeMix(listener, out.data());
    for (float v : out) EXPECT_NEAR(v, 0.5f, 1e-6f);
}
