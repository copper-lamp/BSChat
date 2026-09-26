#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/GlobalMixer.h"
#include "core/audio/JitterBuffer.h"
#include "core/protocol/Message.h"

using namespace bsc::audio;
using namespace bsc::protocol;

namespace {

PlayerId makePlayerId(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

std::vector<uint8_t> makePayload(uint8_t seed, size_t n) {
    std::vector<uint8_t> p(n);
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(seed + i);
    return p;
}

} // namespace

// ---- JitterBuffer ----

TEST(jitter_ordered_flow) {
    JitterBuffer jb;
    jb.push(1, makePayload(1, 4), 0);
    jb.push(2, makePayload(2, 4), 0);
    jb.push(3, makePayload(3, 4), 0);

    auto f1 = jb.pop(0);
    EXPECT_TRUE(f1.has_value());
    EXPECT_EQ((*f1).data[0], 1u);
    auto f2 = jb.pop(0);
    EXPECT_TRUE(f2.has_value());
    EXPECT_EQ((*f2).data[0], 2u);
    auto f3 = jb.pop(0);
    EXPECT_TRUE(f3.has_value());
    EXPECT_EQ((*f3).data[0], 3u);
    EXPECT_FALSE(jb.pop(0).has_value());
    EXPECT_EQ(jb.nextExpectedSeq(), 4u);
}

TEST(jitter_flags_ride_through) {
    JitterBuffer jb;
    jb.push(1, makePayload(1, 2), 0, AudioFlagStart);
    jb.push(3, makePayload(3, 2), 0); // 乱序到达
    jb.push(2, makePayload(2, 2), 0, AudioFlagEnd);

    auto f1 = jb.pop(0);
    EXPECT_TRUE(f1.has_value());
    EXPECT_EQ((*f1).flags, static_cast<uint8_t>(AudioFlagStart));
    auto f2 = jb.pop(0);
    EXPECT_TRUE(f2.has_value());
    EXPECT_EQ((*f2).flags, static_cast<uint8_t>(AudioFlagEnd));
    auto f3 = jb.pop(0);
    EXPECT_TRUE(f3.has_value());
    EXPECT_EQ((*f3).flags, static_cast<uint8_t>(AudioFlagNone));
}

TEST(jitter_out_of_order_and_late_drop) {
    JitterBuffer jb;
    jb.push(1, makePayload(1, 4), 0);
    jb.push(3, makePayload(3, 4), 0); // 乱序：3 先于 2 到达
    jb.push(2, makePayload(2, 4), 0);

    // 首帧定锚 1，乱序帧仍按序输出
    auto f1 = jb.pop(0);
    EXPECT_TRUE(f1.has_value());
    EXPECT_EQ((*f1).data[0], 1u);
    auto f2 = jb.pop(0);
    EXPECT_TRUE(f2.has_value());
    EXPECT_EQ((*f2).data[0], 2u);
    auto f3 = jb.pop(0);
    EXPECT_TRUE(f3.has_value());
    EXPECT_EQ((*f3).data[0], 3u);

    // 迟到帧（seq < 期望 4）直接丢弃
    jb.push(2, makePayload(2, 4), 0);
    EXPECT_EQ(jb.nextExpectedSeq(), 4u);
    EXPECT_EQ(jb.size(), 0u);
}

TEST(jitter_timeout_releases_gap) {
    JitterBuffer jb;
    jb.push(10, makePayload(10, 4), 0);
    // 期望 10，队首即期望 → 直接放行
    auto f = jb.pop(0);
    EXPECT_TRUE(f.has_value());

    // 制造缺口：期望 11，收到 13，等待超时后放行 13
    jb.push(13, makePayload(13, 4), 1000);
    EXPECT_FALSE(jb.pop(1000).has_value()); // 未超时
    auto released = jb.pop(1000 + 201);     // 已超时
    EXPECT_TRUE(released.has_value());
    EXPECT_EQ((*released).data[0], 13u);
    EXPECT_EQ(jb.nextExpectedSeq(), 14u);
}

TEST(jitter_depth_limited) {
    JitterBuffer::Options opts;
    opts.maxDepthFrames = 3;
    JitterBuffer jb(opts);
    for (int i = 1; i <= 6; ++i) {
        jb.push(static_cast<uint64_t>(i), makePayload(static_cast<uint8_t>(i), 1), 0);
    }
    // 深度上限 3：seq 1-3 被挤出，期望从 1 开始，但队首为 4
    EXPECT_EQ(jb.size(), 3u);
    auto f = jb.pop(0);
    EXPECT_TRUE(f.has_value());
    EXPECT_EQ((*f).data[0], 4u);
}

TEST(jitter_clear_resets) {
    JitterBuffer jb;
    jb.push(1, makePayload(1, 2), 0);
    jb.clear();
    EXPECT_EQ(jb.size(), 0u);
    EXPECT_EQ(jb.nextExpectedSeq(), 0u);
    jb.push(9, makePayload(9, 2), 0);
    auto f = jb.pop(0);
    EXPECT_TRUE(f.has_value());
    EXPECT_EQ((*f).data[0], 9u);
}

// ---- GlobalMixer ----

TEST(mixer_single_speaker_passthrough) {
    GlobalMixer mixer({48000, 2880});
    auto id = makePlayerId(1);
    std::vector<float> pcm(2880, 0.5f);
    mixer.addFrame(id, pcm);
    EXPECT_TRUE(mixer.hasActiveTalker());

    std::vector<float> out(2880);
    mixer.mix(out.data());
    // 单路增益 1.0
    EXPECT_NEAR(out[0], 0.5f, 1e-6);
    // 已消费
    EXPECT_FALSE(mixer.hasActiveTalker());
}

TEST(mixer_two_speakers_scaled) {
    GlobalMixer mixer({48000, 2880});
    auto a = makePlayerId(1);
    auto b = makePlayerId(2);
    mixer.addFrame(a, std::vector<float>(2880, 0.5f));
    mixer.addFrame(b, std::vector<float>(2880, 0.5f));

    std::vector<float> out(2880);
    mixer.mix(out.data());
    // 两路增益 1/sqrt(2)，叠加 = 0.5 * 2 / sqrt(2) ≈ 0.7071
    EXPECT_NEAR(out[0], 0.5f * 2.0f / std::sqrt(2.0f), 1e-6);
}

TEST(mixer_no_talker_silence) {
    GlobalMixer mixer({48000, 2880});
    std::vector<float> out(2880, 1.0f);
    mixer.mix(out.data());
    EXPECT_TRUE(out[0] == 0.0f); // 无活跃说话者 → 全零
}

TEST(mixer_clamp_protection) {
    GlobalMixer mixer({48000, 2880});
    // 6 路满幅叠加，应被限幅到 [-1,1] 且不产生 NaN
    for (int i = 1; i <= 6; ++i) {
        mixer.addFrame(makePlayerId(static_cast<uint8_t>(i)), std::vector<float>(2880, 1.0f));
    }
    std::vector<float> out(2880);
    mixer.mix(out.data());
    for (float v : out) {
        EXPECT_TRUE(v >= -1.0f && v <= 1.0f);
        EXPECT_TRUE(!std::isnan(v));
    }
}

TEST(mixer_remove_speaker) {
    GlobalMixer mixer({48000, 2880});
    auto a = makePlayerId(1);
    mixer.addFrame(a, std::vector<float>(2880, 0.5f));
    mixer.removeSpeaker(a);
    EXPECT_FALSE(mixer.hasActiveTalker());
}
