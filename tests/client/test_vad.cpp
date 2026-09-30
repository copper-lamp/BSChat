#include "Harness.h"

#include <cstdint>

#include "client/input/Triggers.h"

using bsc::client::input::VadTrigger;

namespace {

// 单调推进的虚拟时钟。VAD 的迟滞全部按毫秒计时，必须有真实时间差才能被测到。
int64_t g_now = 0;

// 连续喂同一电平若干帧，返回「是否处于说话状态」。每帧 10ms，与 WASAPI 采集粒度一致。
bool feed(VadTrigger& vad, float rms, int frames) {
    for (int i = 0; i < frames; ++i) vad.onLevel(rms, g_now += 10);
    return vad.active();
}

// 统计上行开关次数，用于验证迟滞不会让开关反复抖动。
struct SwitchCounter {
    int switches = 0;
    bool last = false;
    void bind(VadTrigger& vad, float threshold = 0.004F, int holdMs = 350, int onsetMs = 60) {
        vad = VadTrigger(threshold, holdMs, onsetMs, [this](bool active, int64_t) {
            if (active != last) ++switches;
            last = active;
        });
    }
};

} // namespace

// 固定门限的老实现只比 0.018/0.010，安静环境下永远触发不了。
// 自适应噪声底必须让「明显高于本底」的电平触发，哪怕它本身绝对值很小。
TEST(vad_detects_quiet_speech_above_noise_floor) {
    g_now = 0;
    VadTrigger vad(0.004F, 350, 60, {});
    feed(vad, 0.0008F, 200); // 先让检测器学一段本底
    EXPECT_TRUE(feed(vad, 0.008F, 20)); // 0.008 远低于旧固定门限 0.018
}

// 嘈杂环境：本底抬高后仍不能被噪声本身触发，否则会一直开着麦克风。
TEST(vad_ignores_loud_noise_floor) {
    g_now = 0;
    VadTrigger vad(0.004F, 350, 60, {});
    feed(vad, 0.02F, 400);
    EXPECT_FALSE(feed(vad, 0.02F, 100));
    EXPECT_TRUE(feed(vad, 0.05F, 20)); // 本底之上的说话仍要能触发
}

// 起始去抖：敲键盘/碰麦的瞬时尖峰不能开启上行。
TEST(vad_requires_onset_duration) {
    g_now = 0;
    int activations = 0;
    VadTrigger vad(0.004F, 350, 60, [&](bool active, int64_t) {
        if (active) ++activations;
    });
    feed(vad, 0.3F, 2); // 20ms 尖峰，低于 60ms onset
    EXPECT_EQ(activations, 0);
    feed(vad, 0.3F, 10);
    EXPECT_EQ(activations, 1);
}

// 结束保持：句尾的气口不能把一句话切碎（否则 STT 上下文反复重启，字幕碎成很多条）。
TEST(vad_holds_through_short_pauses) {
    g_now = 0;
    VadTrigger vad(0.004F, 350, 60, {});
    feed(vad, 0.0008F, 200);
    EXPECT_TRUE(feed(vad, 0.2F, 20));
    EXPECT_TRUE(feed(vad, 0.0005F, 20)); // 200ms 停顿 < holdMs
    EXPECT_FALSE(feed(vad, 0.0005F, 30));
}

// 迟滞：门限附近的电平不能让上行反复开关。
TEST(vad_hysteresis_avoids_chatter) {
    g_now = 0;
    SwitchCounter counter;
    VadTrigger vad;
    counter.bind(vad, 0.004F);
    feed(vad, 0.0008F, 200);
    feed(vad, 0.2F, 20);
    for (int i = 0; i < 60; ++i) vad.onLevel(i % 2 == 0 ? 0.008F : 0.0006F, g_now += 10);
    EXPECT_TRUE(vad.active());
    EXPECT_EQ(counter.switches, 1);
}

// 噪声底向下跟得快、向上跟得慢：环境突然安静要立刻跟上，长句讲话不能把门限抬上去。
TEST(vad_noise_floor_tracks_quiet_down_fast) {
    g_now = 0;
    VadTrigger vad(0.004F, 350, 60, {});
    feed(vad, 0.01F, 200);
    const float loudFloor = vad.noiseFloor();
    feed(vad, 0.0F, 200);
    EXPECT_TRUE(vad.noiseFloor() < loudFloor * 0.5F);
}

// reset 清空迟滞计时（进出世界、切换说话模式），但保留噪声底（环境没变）。
TEST(vad_reset_clears_hysteresis_keeps_floor) {
    g_now = 0;
    VadTrigger vad(0.004F, 350, 60, {});
    feed(vad, 0.0008F, 200);
    EXPECT_TRUE(feed(vad, 0.2F, 20));
    const float floor = vad.noiseFloor(); // 说话期间噪声底仍在缓慢上爬，取当前值比较
    vad.reset(g_now += 10);
    EXPECT_FALSE(vad.active());
    EXPECT_NEAR(vad.noiseFloor(), floor, 1e-6);
}

// 旧接口（threshold, holdMs）保持可用：onsetMs 取 0，行为与改造前一致。
TEST(vad_legacy_ctor_has_no_onset_debounce) {
    g_now = 0;
    VadTrigger vad(0.5F, 10, {});
    vad.onLevel(1.0F, g_now += 10);
    EXPECT_TRUE(vad.active());
    vad.onLevel(0.0F, g_now += 10);
    EXPECT_TRUE(vad.active());
    vad.onLevel(0.0F, g_now += 10);
    EXPECT_FALSE(vad.active());
}
