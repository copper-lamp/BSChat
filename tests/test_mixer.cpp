#include "Harness.h"
#include "WavBytes.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/codec/OpusCodec.h"
#include "core/pipeline/IStt.h"
#include "core/protocol/Message.h"
#include "server/mixer/ServerMixer.h"
#include "server/session/PlayerSession.h"
#include "server/session/SessionManager.h"

using namespace vc::server;
using namespace vc::codec;
using namespace vc::protocol;
using namespace vc::audio;
using namespace vc::pipeline;

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

// 记录调用的假 STT：验证混音器按 Start/End 驱动流式 begin/feed/end，并可注入结果
class RecordStt : public vc::pipeline::IStt {
public:
    void beginUtterance(const PlayerId& id) override { begins.push_back(id); }
    void feedAudio(const PlayerId& id, const std::vector<float>& pcm) override {
        feeds.push_back(id);
        fedSamples += pcm.size();
    }
    void endUtterance(const PlayerId& id) override { ends.push_back(id); }
    void setResultSink(ResultSink sink) override { sink_ = std::move(sink); }
    bool available() const override { return true; }
    void shutdown() override {}

    // 模拟 worker 产出结果 → 混音器应广播 SttText
    void emit(const PlayerId& id, bool isFinal, std::string text) {
        SttResult r;
        r.speakerId = id;
        r.isFinal   = isFinal;
        r.text      = std::move(text);
        if (sink_) sink_(r);
    }

    std::vector<PlayerId> begins;
    std::vector<PlayerId> feeds;
    std::vector<PlayerId> ends;
    size_t fedSamples = 0;
    ResultSink sink_;
};

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

TEST(mixer_drives_stt_and_broadcasts_text) {
    SessionManager sessions;
    auto id = makePlayerId(3);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});
    RecordStt stt; // 声明于 mixer 之后 → 先于 mixer 析构，符合生命周期契约
    mixer.setStt(&stt);

    auto data = encodeTone();
    sessions.find(id)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    sessions.find(id)->pushAudio(makeAudio(2, AudioFlagNone, data), 1000);
    sessions.find(id)->pushAudio(makeAudio(3, AudioFlagEnd, data), 1000);

    mixer.tickOnce(1000);

    // 流式驱动：Start → begin、非空帧 → feed、End → end
    EXPECT_EQ(stt.begins.size(), 1u);
    EXPECT_EQ(stt.feeds.size(), 3u); // 3 帧均带数据
    EXPECT_EQ(stt.feeds[0], id);
    EXPECT_EQ(stt.ends.size(), 1u);

    // STT 结果（部分 + 最终）→ 广播 SttText
    stt.emit(id, false, "部分");
    stt.emit(id, true, "最终");
    auto out = collect(mixer);

    size_t mixCount = 0, textCount = 0;
    for (auto& [peer, msg] : out) {
        (void)peer;
        if (std::holds_alternative<MixStreamMessage>(msg)) ++mixCount;
        if (auto* t = std::get_if<SttTextMessage>(&msg)) {
            ++textCount;
            EXPECT_EQ(t->speakerId, id);
            if (t->isFinal) {
                EXPECT_EQ(t->text, "最终");
            } else {
                EXPECT_EQ(t->text, "部分");
            }
        }
    }
    EXPECT_EQ(mixCount, 2u);  // 120ms tick = 2 × 60ms 混音帧
    EXPECT_EQ(textCount, 2u);
}

TEST(mixer_stt_unavailable_keeps_voice_path) {
    // STT 不可用（如引擎缺失）→ 混音链路不受影响，仅跳过 STT 驱动
    SessionManager sessions;
    auto id = makePlayerId(4);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});
    RecordStt stt; // available() 恒 true，这里验证 setStt(nullptr) 分支
    mixer.setStt(nullptr);

    auto data = encodeTone();
    sessions.find(id)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);

    mixer.tickOnce(1000);
    auto out = collect(mixer);
    EXPECT_TRUE(out.size() >= 1u); // 混音流照常
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

// 文件声源是独立声源：即使接收者自己没有上行，也应收到带音频的 MixStream
// （即不被“不把自己的声音混给自己”的自我抑制影响）。
TEST(mixer_file_playback_reaches_receiver_without_uplink) {
    SessionManager sessions;
    auto id = makePlayerId(1);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});

    std::vector<float> samples(static_cast<size_t>(kFrameSamples) * 2, 0.4F);
    auto bytes = vc::test::buildWavBytes(kSampleRate, 1, true, samples);
    auto path = vc::test::writeTempWav(bytes, "voicechat-test-mixer-playback.wav");

    std::string error;
    EXPECT_TRUE(mixer.startFilePlayback(path, error));
    EXPECT_TRUE(mixer.filePlaybackActive());

    mixer.tickOnce(0);
    auto out = collect(mixer);
    size_t mixStreams = 0;
    for (auto const& [peer, message] : out) {
        if (std::holds_alternative<MixStreamMessage>(message)) {
            ++mixStreams;
            EXPECT_TRUE(peer == id);
        }
    }
    EXPECT_TRUE(mixStreams > 0u);

    mixer.stopFilePlayback();
    EXPECT_FALSE(mixer.filePlaybackActive());

    std::filesystem::remove(path);
}
