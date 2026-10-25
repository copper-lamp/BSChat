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

using namespace bsc::server;
using namespace bsc::codec;
using namespace bsc::protocol;
using namespace bsc::audio;
using namespace bsc::pipeline;

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
class RecordStt : public bsc::pipeline::IStt {
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
    auto listener = makePlayerId(9);
    PlayerSession::Options opts;
    sessions.addSession(id, opts);
    sessions.addSession(listener, opts);
    ServerMixer mixer(sessions, {});

    auto data = encodeTone();
    sessions.find(id)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    sessions.find(id)->pushAudio(makeAudio(2, AudioFlagNone, data), 1000);

    mixer.tickOnce(1000);
    auto out = collect(mixer);

    // 每帧上行各混一路：活跃时每帧一条 MixStream，且只发给能听见的接收者。
    // 说话者本人自我抑制，不会收到自己的语音。
    EXPECT_EQ(out.size(), 2u);
    if (out.size() < 2) return;
    EXPECT_TRUE(out[0].first == listener);
    EXPECT_TRUE(out[1].first == listener);
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
    auto listener = makePlayerId(30);
    sessions.addSession(id, {});
    sessions.addSession(listener, {});
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

    // STT 结果（部分 + 最终）→ 发给说话者本人与能听见他的接收者
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
    // 3 帧上行 → 3 个下行帧（每个子帧各取一帧、不重复编码）；队列取空后不再出帧。
    // 每条识别结果发给 2 个会话（说话者本人 + 听者）。
    EXPECT_EQ(mixCount, 3u);
    EXPECT_EQ(textCount, 4u);
}

// 字幕前缀（说话人名字）必须跟着识别结果一起下发，并且每条结果都要落日志：
// 少了名字就是「字幕没有玩家名字」，少了日志就是这个问题在现场完全无法定位。
TEST(mixer_attaches_speaker_name_and_logs_every_result) {
    SessionManager sessions;
    auto id = makePlayerId(20);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});
    RecordStt stt;
    mixer.setStt(&stt);
    std::vector<std::string> logs;
    std::vector<bool> warns;
    mixer.setLog([&](bool isError, const std::string& message) {
        warns.push_back(isError);
        logs.push_back(message);
    });

    mixer.setSpeakerName(id, "Steve");
    stt.emit(id, false, "部分");
    stt.emit(id, true, "最终");
    auto out = collect(mixer);

    int textCount = 0;
    for (auto& [peer, msg] : out) {
        (void)peer;
        if (auto* t = std::get_if<SttTextMessage>(&msg)) {
            ++textCount;
            EXPECT_EQ(t->speakerName, std::string("Steve"));
        }
    }
    EXPECT_EQ(textCount, 2);
    EXPECT_EQ(logs.size(), 2u);
    EXPECT_TRUE(logs[0].find("name=\"Steve\"") != std::string::npos);
    EXPECT_TRUE(logs[0].find("text=\"部分\"") != std::string::npos);
    EXPECT_TRUE(logs[1].find("kind=final") != std::string::npos);
    EXPECT_FALSE(warns[0]);
    EXPECT_FALSE(warns[1]);
}

// 没登记名字时按 warn 落日志（而不是静默发出空名字），并且空名不覆盖已有名字。
TEST(mixer_warns_on_missing_speaker_name) {
    SessionManager sessions;
    auto id = makePlayerId(21);
    sessions.addSession(id, {});
    ServerMixer mixer(sessions, {});
    RecordStt stt;
    mixer.setStt(&stt);
    std::vector<bool> warns;
    mixer.setLog([&](bool isError, const std::string&) { warns.push_back(isError); });

    mixer.setSpeakerName(id, "Steve");
    mixer.setSpeakerName(id, {}); // 入服早期取不到名字是常态，不能把已有名字抹掉
    stt.emit(id, true, "最终");

    auto out = collect(mixer);
    for (auto& [peer, msg] : out) {
        (void)peer;
        if (auto* t = std::get_if<SttTextMessage>(&msg)) EXPECT_EQ(t->speakerName, std::string("Steve"));
    }
    EXPECT_EQ(warns.size(), 1u);
    EXPECT_FALSE(warns[0]);
}

TEST(mixer_stt_unavailable_keeps_voice_path) {
    // STT 不可用（如引擎缺失）→ 混音链路不受影响，仅跳过 STT 驱动
    SessionManager sessions;
    auto id = makePlayerId(4);
    auto listener = makePlayerId(40);
    sessions.addSession(id, {});
    sessions.addSession(listener, {});
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
    auto bytes = bsc::test::buildWavBytes(kSampleRate, 1, true, samples);
    auto path = bsc::test::writeTempWav(bytes, "bschat-test-mixer-playback.wav");

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

// 混音节拍按 config_.tickMs 限流：驱动器（服务器 tick 50ms）比混音周期快时不能被放大下行速率。
TEST(mixer_tick_gated_by_configured_period) {
    SessionManager sessions;
    sessions.addSession(makePlayerId(1), {});
    ServerMixer mixer(sessions, {});

    std::vector<float> samples(static_cast<size_t>(kFrameSamples) * 8, 0.4F);
    auto bytes = bsc::test::buildWavBytes(kSampleRate, 1, true, samples);
    auto path = bsc::test::writeTempWav(bytes, "bschat-test-mixer-pacing.wav");
    std::string error;
    EXPECT_TRUE(mixer.startFilePlayback(path, error));

    mixer.tickOnce(0);
    EXPECT_TRUE(!collect(mixer).empty()); // 首次立即混音

    mixer.tickOnce(50); // 未到下一个混音周期（默认 120ms）
    EXPECT_TRUE(collect(mixer).empty());

    mixer.tickOnce(120);
    EXPECT_TRUE(!collect(mixer).empty());

    std::filesystem::remove(path);
}

// 同一 tick 内的多个子帧必须各用一段不同音频：此前 drain 后覆盖式喂入会把同一帧编码多次。
TEST(mixer_sub_frames_use_distinct_audio) {    SessionManager sessions;
    sessions.addSession(makePlayerId(1), {});
    ServerMixer mixer(sessions, {});

    // 第 1 帧恒 +0.4、第 2 帧恒 -0.4（其余静音）：两帧内容不同，编码结果也必须不同。
    // 长度要够本 tick 的每个子帧各取一帧，否则后面几个子帧没有声源、不会出帧。
    const size_t framesPerTick = static_cast<size_t>(kMixTickMs / kFrameSizeMs);
    std::vector<float> samples(static_cast<size_t>(kFrameSamples) * framesPerTick, 0.0F);
    for (int i = 0; i < kFrameSamples; ++i) {
        samples[static_cast<size_t>(i)] = 0.4F;
        samples[static_cast<size_t>(kFrameSamples) + static_cast<size_t>(i)] = -0.4F;
    }
    auto bytes = bsc::test::buildWavBytes(kSampleRate, 1, true, samples);
    auto path = bsc::test::writeTempWav(bytes, "bschat-test-mixer-subframes.wav");
    std::string error;
    EXPECT_TRUE(mixer.startFilePlayback(path, error));

    mixer.tickOnce(0);
    std::vector<std::vector<uint8_t>> payloads;
    for (auto const& [peer, message] : collect(mixer)) {
        if (auto const* mix = std::get_if<MixStreamMessage>(&message)) payloads.push_back(mix->opusData);
    }
    EXPECT_EQ(payloads.size(), framesPerTick);
    if (payloads.size() == framesPerTick) EXPECT_TRUE(payloads[0] != payloads[1]);

    std::filesystem::remove(path);
}

namespace {

void setPosition(SessionManager& sessions, PlayerId id, float x, int32_t dimensionId, int64_t atMs) {
    PosUpdateMessage pos;
    pos.playerId = id;
    pos.x = x;
    pos.y = 0.0f;
    pos.z = 0.0f;
    pos.dimensionId = dimensionId;
    pos.sendAtMs = 0;
    sessions.find(id)->updatePosition(pos, atMs);
}

ServerMixer::Config proximityConfig() {
    ServerMixer::Config config;
    config.spatial.mode = MixMode::Proximity;
    config.spatial.attenuationRadiusBlocks = 8.0f;
    config.spatial.maxDistanceBlocks = 16.0f;
    config.spatial.dimensionIsolation = true;
    // 字幕门控用的是真实稳态时钟，测试里把位置新鲜度阈值放宽到不会过期。
    config.staleMs = 1'000'000'000'000;
    return config;
}

} // namespace

// 超出最大传播距离：听不到语音，也收不到该说话者的字幕；说话者本人仍能看到自己的字幕。
TEST(mixer_proximity_gates_audio_and_subtitles) {
    SessionManager sessions;
    auto listener = makePlayerId(1);
    auto speaker = makePlayerId(2);
    sessions.addSession(listener, {});
    sessions.addSession(speaker, {});
    sessions.find(listener)->setProximityEnabled(true);
    sessions.find(speaker)->setProximityEnabled(true);
    setPosition(sessions, listener, 0.0f, 0, 1000);
    setPosition(sessions, speaker, 40.0f, 0, 1000);

    ServerMixer mixer(sessions, proximityConfig());
    RecordStt stt;
    mixer.setStt(&stt);

    auto data = encodeTone();
    sessions.find(speaker)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    sessions.find(speaker)->pushAudio(makeAudio(2, AudioFlagNone, data), 1000);
    mixer.tickOnce(1000);

    stt.emit(speaker, true, "远处的字");
    auto out = collect(mixer);

    for (auto const& [peer, message] : out) {
        EXPECT_FALSE(peer == listener); // 听不见 → 既无混音也无字幕
        if (auto const* text = std::get_if<SttTextMessage>(&message)) {
            EXPECT_TRUE(peer == speaker);
            EXPECT_EQ(text->text, std::string("远处的字"));
        }
    }
}

// 距离在衰减半径内：听者收到混音流与字幕；说话者本人不收到自己的混音（自我抑制）。
TEST(mixer_proximity_nearby_delivers_audio_and_subtitles) {
    SessionManager sessions;
    auto listener = makePlayerId(1);
    auto speaker = makePlayerId(2);
    sessions.addSession(listener, {});
    sessions.addSession(speaker, {});
    sessions.find(listener)->setProximityEnabled(true);
    sessions.find(speaker)->setProximityEnabled(true);
    setPosition(sessions, listener, 0.0f, 0, 1000);
    setPosition(sessions, speaker, 4.0f, 0, 1000);

    ServerMixer mixer(sessions, proximityConfig());
    RecordStt stt;
    mixer.setStt(&stt);

    auto data = encodeTone();
    sessions.find(speaker)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    mixer.tickOnce(1000);
    stt.emit(speaker, false, "近处的字");
    auto out = collect(mixer);

    bool listenerHasMix = false;
    bool listenerHasText = false;
    bool speakerHasOwnText = false;
    for (auto const& [peer, message] : out) {
        if (std::holds_alternative<MixStreamMessage>(message)) {
            if (peer == listener) listenerHasMix = true;
            EXPECT_FALSE(peer == speaker);
        }
        if (auto const* text = std::get_if<SttTextMessage>(&message)) {
            if (peer == listener) listenerHasText = true;
            if (peer == speaker) speakerHasOwnText = true;
            EXPECT_EQ(text->text, std::string("近处的字"));
        }
    }
    EXPECT_TRUE(listenerHasMix);
    EXPECT_TRUE(listenerHasText);
    EXPECT_TRUE(speakerHasOwnText);
}

// 跨维度：即便距离为 0 也听不到，字幕不下发。
TEST(mixer_proximity_isolates_dimensions) {
    SessionManager sessions;
    auto listener = makePlayerId(1);
    auto speaker = makePlayerId(2);
    sessions.addSession(listener, {});
    sessions.addSession(speaker, {});
    sessions.find(listener)->setProximityEnabled(true);
    sessions.find(speaker)->setProximityEnabled(true);
    setPosition(sessions, listener, 0.0f, 0, 1000);
    setPosition(sessions, speaker, 0.0f, 1, 1000);

    ServerMixer mixer(sessions, proximityConfig());
    RecordStt stt;
    mixer.setStt(&stt);
    auto data = encodeTone();
    sessions.find(speaker)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    mixer.tickOnce(1000);
    stt.emit(speaker, true, "异界");

    auto out = collect(mixer);
    for (auto const& [peer, message] : out) {
        EXPECT_FALSE(peer == listener);
        (void)message;
    }
}

// 未协商到近距离能力的旧会话按全局混音降级：不能因为服务端开了近距离就让它听不到远处玩家。
TEST(mixer_proximity_falls_back_to_global_for_legacy_session) {
    SessionManager sessions;
    auto listener = makePlayerId(1);
    auto speaker = makePlayerId(2);
    sessions.addSession(listener, {});
    sessions.addSession(speaker, {});
    sessions.find(listener)->setProximityEnabled(false); // 旧客户端
    sessions.find(speaker)->setProximityEnabled(true);
    setPosition(sessions, listener, 0.0f, 0, 1000);
    setPosition(sessions, speaker, 400.0f, 0, 1000);

    ServerMixer mixer(sessions, proximityConfig());
    auto data = encodeTone();
    sessions.find(speaker)->pushAudio(makeAudio(1, AudioFlagStart, data), 1000);
    mixer.tickOnce(1000);

    bool listenerHasMix = false;
    for (auto const& [peer, message] : collect(mixer)) {
        if (peer == listener && std::holds_alternative<MixStreamMessage>(message)) listenerHasMix = true;
    }
    EXPECT_TRUE(listenerHasMix);
}
