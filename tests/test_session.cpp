#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/codec/OpusCodec.h"
#include "core/protocol/Message.h"
#include "server/session/PlayerSession.h"
#include "server/session/SessionManager.h"

using namespace vc::server;
using namespace vc::codec;
using namespace vc::protocol;
using namespace vc::audio;

namespace {

constexpr int kSampleRate = kDefaultSampleRate;
constexpr int kFrameSizeMs = kDefaultFrameSizeMs;
constexpr int kFrameSamples = kSampleRate * kFrameSizeMs / 1000; // 2880

PlayerId makePlayerId(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

// 生成一帧有能量的 PCM（440Hz 音调）
std::vector<float> makeToneFrame() {
    std::vector<float> pcm(kFrameSamples);
    for (int i = 0; i < kFrameSamples; ++i) {
        pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / kSampleRate));
    }
    return pcm;
}

// 编码为压缩帧；返回压缩数据
std::vector<uint8_t> encodeTone() {
    OpusEncoder enc(kSampleRate, 1, kFrameSamples, kDefaultBitrateKbps);
    auto pcm = makeToneFrame();
    std::vector<uint8_t> packet(enc.maxPacketSize());
    int n = enc.encode(pcm.data(), packet.data(), packet.size());
    EXPECT_TRUE(n > 0);
    packet.resize(static_cast<size_t>(n));
    return packet;
}

double rms(const std::vector<float>& pcm) {
    if (pcm.empty()) return 0.0;
    double sum = 0.0;
    for (float v : pcm) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(pcm.size()));
}

AudioDataMessage makeAudio(uint64_t seq, uint8_t flags, const std::vector<uint8_t>& data) {
    AudioDataMessage msg;
    msg.seq = seq;
    msg.flags = flags;
    msg.opusData = data;
    return msg;
}

} // namespace

TEST(session_audio_frame_decodes_for_mix) {
    PlayerSession::Options opts;
    PlayerSession session(makePlayerId(1), opts);
    auto data = encodeTone();

    session.pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    auto frame = session.pollFrame(1000);
    EXPECT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->empty());
    EXPECT_TRUE(rms(*frame) > 0.1); // 解码出明显能量
}

TEST(session_utterance_segmentation) {
    PlayerSession::Options opts;
    PlayerSession session(makePlayerId(1), opts);
    auto data = encodeTone();

    session.pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    session.pushAudio(makeAudio(2, AudioFlagNone, data), 1000);
    session.pushAudio(makeAudio(3, AudioFlagEnd, data), 1000);

    // 逐帧拉取解码 PCM
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    auto last = session.pollFrame(1000);
    EXPECT_TRUE(last.has_value());

    // End 帧后产生完整话语（3 帧拼接）
    auto utterance = session.pollUtterance();
    EXPECT_TRUE(utterance.has_value());
    EXPECT_EQ(utterance->size(), static_cast<size_t>(3 * kFrameSamples));
    EXPECT_TRUE(rms(*utterance) > 0.1);

    // 无更多话语
    EXPECT_FALSE(session.pollUtterance().has_value());
}

TEST(session_flag_only_frames_are_silence) {
    PlayerSession::Options opts;
    PlayerSession session(makePlayerId(2), opts);

    // 仅标志帧（无数据）：静音不推流时客户端仍发 Start/End 标志
    session.pushAudio(makeAudio(1, AudioFlagStart, {}), 1000);
    auto f1 = session.pollFrame(1000);
    EXPECT_TRUE(f1.has_value());
    EXPECT_TRUE(f1->empty()); // 静音帧 → 空 PCM

    session.pushAudio(makeAudio(2, AudioFlagEnd, {}), 1000);
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_FALSE(session.pollUtterance().has_value()); // 空句不入队
}

TEST(session_start_without_end_keeps_partial_until_end) {
    PlayerSession::Options opts;
    PlayerSession session(makePlayerId(3), opts);
    auto data = encodeTone();

    session.pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    session.pushAudio(makeAudio(2, AudioFlagNone, data), 1000);
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_FALSE(session.pollUtterance().has_value()); // 未 End，无完整话语

    // 松开发尾标志帧（无数据）→ 收尾
    session.pushAudio(makeAudio(3, AudioFlagEnd, {}), 1000);
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    auto utterance = session.pollUtterance();
    EXPECT_TRUE(utterance.has_value());
    EXPECT_EQ(utterance->size(), static_cast<size_t>(2 * kFrameSamples));
}

TEST(session_max_utterance_forced_split) {
    PlayerSession::Options opts;
    opts.maxUtteranceMs = 120; // 2 帧即强制切分
    PlayerSession session(makePlayerId(4), opts);
    auto data = encodeTone();

    // 3 帧无 End：第 2 帧累积达上限 → 强制切分，第 3 帧进入新句
    session.pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    session.pushAudio(makeAudio(2, AudioFlagNone, data), 1000);
    session.pushAudio(makeAudio(3, AudioFlagNone, data), 1000);
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    EXPECT_TRUE(session.pollFrame(1000).has_value());

    auto seg1 = session.pollUtterance();
    EXPECT_TRUE(seg1.has_value());
    EXPECT_EQ(seg1->size(), static_cast<size_t>(2 * kFrameSamples)); // 前 2 帧

    // 第 3 帧进入新句，第 4 帧（End）累积达上限并收尾
    session.pushAudio(makeAudio(4, AudioFlagEnd, data), 1000);
    EXPECT_TRUE(session.pollFrame(1000).has_value());
    auto seg2 = session.pollUtterance();
    EXPECT_TRUE(seg2.has_value());
    EXPECT_EQ(seg2->size(), static_cast<size_t>(2 * kFrameSamples)); // 第 3、4 帧
}

TEST(session_rate_limit_drops_flood) {
    PlayerSession::Options opts;
    opts.maxFramesPerSecond = 5;
    opts.maxDepthFrames = 100; // 拉大深度，让限速成为唯一瓶颈
    PlayerSession session(makePlayerId(5), opts);
    auto data = encodeTone();

    // 同一秒内连推 20 帧 → 只应放行 5 帧
    for (uint64_t i = 1; i <= 20; ++i) {
        session.pushAudio(makeAudio(i, AudioFlagNone, data), 1000);
    }
    EXPECT_EQ(session.pendingFrames(), 5u);

    // 顺序拉出恰好 5 帧，第 6 帧无
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(session.pollFrame(1000).has_value());
    }
    EXPECT_FALSE(session.pollFrame(1000).has_value());
}

TEST(session_reset_clears_state) {
    PlayerSession::Options opts;
    PlayerSession session(makePlayerId(6), opts);
    auto data = encodeTone();

    session.pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    session.reset();
    EXPECT_EQ(session.pendingFrames(), 0u);
    EXPECT_FALSE(session.pollFrame(1000).has_value());
    EXPECT_FALSE(session.pollUtterance().has_value());
}

TEST(session_manager_add_find_remove) {
    SessionManager manager;
    auto id = makePlayerId(7);

    PlayerSession::Options opts;
    manager.addSession(id, opts);
    EXPECT_EQ(manager.size(), 1u);
    auto session = manager.find(id);
    EXPECT_TRUE(session != nullptr);
    EXPECT_EQ(session->id(), id);

    manager.removeSession(id);
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_TRUE(manager.find(id) == nullptr);
}

TEST(session_manager_snapshot_keeps_alive) {
    SessionManager manager;
    auto id = makePlayerId(8);

    PlayerSession::Options opts;
    manager.addSession(id, opts);
    auto snap = manager.snapshot();
    EXPECT_EQ(snap.size(), 1u);

    manager.removeSession(id); // 移除后快照仍保活
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(snap[0]->id(), id);
}
