#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/codec/OpusCodec.h"
#include "core/protocol/Message.h"
#include "server/mixer/ServerMixer.h"
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

std::vector<float> makeToneFrame() {
    std::vector<float> pcm(kFrameSamples);
    for (int i = 0; i < kFrameSamples; ++i) {
        pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / kSampleRate));
    }
    return pcm;
}

std::vector<uint8_t> encodeTone() {
    OpusEncoder enc(kSampleRate, 1, kFrameSamples, kDefaultBitrateKbps);
    auto pcm = makeToneFrame();
    std::vector<uint8_t> packet(enc.maxPacketSize());
    int n = enc.encode(pcm.data(), packet.data(), packet.size());
    EXPECT_TRUE(n > 0);
    packet.resize(static_cast<size_t>(n));
    return packet;
}

AudioDataMessage makeAudio(uint64_t seq, uint8_t flags, const std::vector<uint8_t>& data) {
    AudioDataMessage msg;
    msg.seq = seq;
    msg.flags = flags;
    msg.opusData = data;
    return msg;
}

// 收集 drainPending 派发的消息
std::vector<std::pair<PlayerId, Message>> collect(ServerMixer& mixer) {
    std::vector<std::pair<PlayerId, Message>> out;
    mixer.drainPending([&out](const PlayerId& id, const Message& msg) { out.emplace_back(id, msg); });
    return out;
}

} // namespace

TEST(mixer_produces_mix_stream_when_active) {
    SessionManager sessions;
    auto id = makePlayerId(1);
    PlayerSession::Options opts;
    sessions.addSession(id, opts);
    ServerMixer mixer(sessions, {});

    auto data = encodeTone();
    sessions.find(id)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    sessions.find(id)->pushAudio(makeAudio(2, AudioFlagNone, data), 1000);

    mixer.tickOnce(1000);
    auto out = collect(mixer);

    // 120ms tick = 2 × 60ms 混音帧，活跃时每 tick 2 条 MixStream
    EXPECT_EQ(out.size(), 2u);
    auto seq1 = std::get_if<MixStreamMessage>(&out[0].second);
    auto seq2 = std::get_if<MixStreamMessage>(&out[1].second);
    EXPECT_TRUE(seq1 != nullptr && seq2 != nullptr);
    EXPECT_EQ(seq1->seq, 1u);
    EXPECT_EQ(seq2->seq, 2u);
    EXPECT_FALSE(seq1->opusData.empty());
    EXPECT_FALSE(seq2->opusData.empty());
}

TEST(mixer_silent_when_no_talker) {
    SessionManager sessions;
    sessions.addSession(makePlayerId(2), {});
    ServerMixer mixer(sessions, {});

    mixer.tickOnce(1000); // 无任何帧
    auto out = collect(mixer);
    EXPECT_TRUE(out.empty());
}

TEST(mixer_utterance_sink_fires) {
    SessionManager sessions;
    auto id = makePlayerId(3);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});

    bool fired = false;
    PlayerId sinkId{};
    size_t sinkSamples = 0;
    mixer.setUtteranceSink([&](const PlayerId& sid, std::vector<float> pcm) {
        fired = true;
        sinkId = sid;
        sinkSamples = pcm.size();
    });

    auto data = encodeTone();
    sessions.find(id)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    sessions.find(id)->pushAudio(makeAudio(2, AudioFlagEnd, data), 1000);

    mixer.tickOnce(1000);
    EXPECT_TRUE(fired);
    EXPECT_EQ(sinkId, id);
    EXPECT_EQ(sinkSamples, static_cast<size_t>(2 * kFrameSamples));
}

TEST(mixer_backpressure_drops_oldest) {
    SessionManager sessions;
    for (uint8_t i = 1; i <= 3; ++i) {
        sessions.addSession(makePlayerId(i), {});
    }
    ServerMixer::Config config;
    config.maxPending = 2;
    ServerMixer mixer(sessions, config);

    auto data = encodeTone();
    for (uint8_t i = 1; i <= 3; ++i) {
        sessions.find(makePlayerId(i))->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    }

    mixer.tickOnce(1000);
    auto out = collect(mixer);
    EXPECT_TRUE(out.size() <= 2u); // 队列上限生效
}
