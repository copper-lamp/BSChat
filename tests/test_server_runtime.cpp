#include "Harness.h"

#include <cmath>
#include <utility>
#include <vector>

#include "core/codec/OpusCodec.h"
#include "core/audio/AudioTypes.h"
#include "core/pipeline/ITransport.h"
#include "core/protocol/Message.h"
#include "server/entry/ServerRuntime.h"

using namespace bsc::codec;
using namespace bsc::config;
using namespace bsc::pipeline;
using namespace bsc::protocol;
using namespace bsc::server;

namespace {

PlayerId makePlayerId(uint8_t value) {
    PlayerId id{};
    id[0] = value;
    return id;
}

std::vector<uint8_t> encodeTone() {
    // 跟随产品默认音频参数：编码帧长必须与会话解码帧长一致，否则该帧解不出音频
    constexpr int sampleRate = bsc::audio::kDefaultSampleRate;
    constexpr int frameSamples = bsc::audio::samplesPerFrame(sampleRate, bsc::audio::kDefaultFrameSizeMs);
    OpusEncoder encoder(sampleRate, 1, frameSamples, bsc::audio::kDefaultBitrateKbps);
    std::vector<float> pcm(frameSamples);
    for (int i = 0; i < frameSamples; ++i) {
        pcm[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sampleRate));
    }
    std::vector<uint8_t> packet(encoder.maxPacketSize());
    int size = encoder.encode(pcm.data(), packet.data(), packet.size());
    EXPECT_TRUE(size > 0);
    packet.resize(static_cast<size_t>(size));
    return packet;
}

class FakeTransport final : public ITransport {
public:
    void send(const PlayerId& peerId, const Message& message) override {
        sent.emplace_back(peerId, message);
    }

    void setMessageHandler(MessageHandler handler) override {
        handler_ = std::move(handler);
    }

    void inject(const PlayerId& peerId, const Message& message) {
        handler_(peerId, message);
    }

    std::vector<std::pair<PlayerId, Message>> sent;
    MessageHandler handler_;
};

} // namespace

TEST(server_runtime_hello_creates_session_and_sends_welcome) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(1);

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    hello.capabilities = CapabilityPtt | CapabilitySubtitle;
    hello.sampleRate = bsc::audio::kDefaultSampleRate;
    hello.frameSizeMs = bsc::audio::kDefaultFrameSizeMs;
    transport.inject(id, hello);

    EXPECT_EQ(runtime.sessionCount(), 1u);
    EXPECT_EQ(transport.sent.size(), 1u);
    auto* welcome = std::get_if<WelcomeMessage>(&transport.sent[0].second);
    EXPECT_TRUE(welcome != nullptr);
    EXPECT_EQ(welcome->protocolVersion, kProtocolVersion);
    EXPECT_FALSE(welcome->sttEnabled);
}

TEST(server_runtime_logs_speech_summary_once_on_ptt_release) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(3);
    std::vector<std::string> logs;
    runtime.setLogSink([&](bool, const std::string& message) { logs.push_back(message); });

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);
    logs.clear();

    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    AudioDataMessage audio;
    audio.seq = 1;
    audio.opusData = {1, 2, 3};
    transport.inject(id, audio);
    transport.inject(id, ControlMessage{ControlType::PttReleased, 0});

    size_t summaries = 0;
    for (const auto& log : logs) {
        if (log.find("[speech] summary") != std::string::npos) {
            ++summaries;
            EXPECT_TRUE(log.find("speaker=") != std::string::npos);
            EXPECT_TRUE(log.find("received_frames=1") != std::string::npos);
            EXPECT_TRUE(log.find("accepted_frames=1") != std::string::npos);
            EXPECT_TRUE(log.find("opus_bytes=3") != std::string::npos);
            EXPECT_TRUE(log.find("reason=ptt_released") != std::string::npos);
        }
        EXPECT_TRUE(log.find("rate limit or invalid frame") == std::string::npos);
    }
    EXPECT_EQ(summaries, 1u);
}

TEST(server_runtime_stop_closes_speech_even_when_not_started) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(6);
    std::vector<std::string> logs;
    runtime.setLogSink([&](bool, const std::string& message) { logs.push_back(message); });
    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);
    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    runtime.stop();
    runtime.stop();
    size_t summaries = 0;
    for (const auto& log : logs) {
        if (log.find("[speech] summary") != std::string::npos) {
            ++summaries;
            EXPECT_TRUE(log.find("reason=runtime_stopped") != std::string::npos);
        }
    }
    EXPECT_EQ(summaries, 1u);
}

TEST(server_runtime_counts_evicted_frames_in_dropped_total) {
    FakeTransport transport;
    ServerConfig config;
    config.jitterMaxDepthFrames = 1;
    ServerRuntime runtime(transport, config);
    auto id = makePlayerId(7);
    std::vector<std::string> logs;
    runtime.setLogSink([&](bool, const std::string& message) { logs.push_back(message); });
    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);
    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    AudioDataMessage audio;
    audio.seq = 1;
    audio.opusData = {1, 2, 3};
    transport.inject(id, audio);
    audio.seq = 2;
    transport.inject(id, audio);
    transport.inject(id, ControlMessage{ControlType::PttReleased, 0});
    bool found = false;
    for (const auto& log : logs) {
        if (log.find("[speech] summary") != std::string::npos) {
            found = true;
            EXPECT_TRUE(log.find("accepted_frames=2") != std::string::npos);
            EXPECT_TRUE(log.find("evicted_frames=1") != std::string::npos);
            EXPECT_TRUE(log.find("dropped_frames=1") != std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}

TEST(server_runtime_stop_closes_active_speech_summary) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(5);
    std::vector<std::string> logs;
    runtime.setLogSink([&](bool, const std::string& message) { logs.push_back(message); });
    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);
    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    runtime.start();
    runtime.stop();
    size_t summaries = 0;
    for (const auto& log : logs) {
        if (log.find("[speech] summary") != std::string::npos) {
            ++summaries;
            EXPECT_TRUE(log.find("reason=runtime_stopped") != std::string::npos);
        }
    }
    EXPECT_EQ(summaries, 1u);
}

TEST(server_runtime_repeated_ptt_messages_do_not_duplicate_summaries) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(4);
    std::vector<std::string> logs;
    runtime.setLogSink([&](bool, const std::string& message) { logs.push_back(message); });
    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);
    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    transport.inject(id, ControlMessage{ControlType::PttPressed, 0});
    transport.inject(id, ControlMessage{ControlType::PttReleased, 0});
    transport.inject(id, ControlMessage{ControlType::PttReleased, 0});
    size_t summaries = 0;
    for (const auto& log : logs) summaries += log.find("[speech] summary") != std::string::npos;
    EXPECT_EQ(summaries, 1u);
}

TEST(server_runtime_audio_reaches_mixer_without_stt) {
    FakeTransport transport;
    ServerConfig config;
    config.sttEnabled = true;
    ServerRuntime runtime(transport, config);
    auto id = makePlayerId(2);

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    hello.sampleRate = bsc::audio::kDefaultSampleRate;
    hello.frameSizeMs = bsc::audio::kDefaultFrameSizeMs;
    transport.inject(id, hello);
    transport.sent.clear();

    auto data = encodeTone();
    AudioDataMessage audio;
    audio.seq = 1;
    audio.flags = AudioFlagStart;
    audio.opusData = data;
    transport.inject(id, audio);

    runtime.tickOnce(1000);
    runtime.drainPending();

    EXPECT_TRUE(runtime.sessionCount() == 1u);
    EXPECT_TRUE(transport.sent.size() >= 1u);
    bool hasMix = false;
    for (const auto& [peer, message] : transport.sent) {
        (void)peer;
        hasMix = hasMix || std::holds_alternative<MixStreamMessage>(message);
    }
    EXPECT_TRUE(hasMix);
}

TEST(server_runtime_rejects_handshake_version_mismatch) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(8);

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = static_cast<uint8_t>(kProtocolVersion + 1);
    hello.capabilities = CapabilityPtt;
    transport.inject(id, hello);

    EXPECT_EQ(runtime.sessionCount(), 0u);
    EXPECT_EQ(transport.sent.size(), 0u);
}

TEST(server_runtime_rejects_handshake_identity_mismatch) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(9);

    HelloMessage hello;
    hello.playerId = makePlayerId(10); // 与传输层报出的 peer 不一致
    hello.protocolVersion = kProtocolVersion;
    transport.inject(id, hello);

    EXPECT_EQ(runtime.sessionCount(), 0u);
    EXPECT_EQ(transport.sent.size(), 0u);
}

TEST(server_runtime_ignores_business_messages_before_handshake) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(11);

    AudioDataMessage audio;
    audio.seq = 1;
    audio.opusData = {1, 2, 3};
    transport.inject(id, audio);

    PosUpdateMessage pos;
    pos.playerId = id;
    pos.x = 1.0f;
    pos.y = 2.0f;
    pos.z = 3.0f;
    transport.inject(id, pos);

    runtime.tickOnce(1000);
    runtime.drainPending();

    EXPECT_EQ(runtime.sessionCount(), 0u);
    EXPECT_EQ(transport.sent.size(), 0u);
}

TEST(server_runtime_negotiates_only_common_capabilities) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{}); // STT 默认关闭
    auto id = makePlayerId(12);

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    hello.capabilities = CapabilityPtt | CapabilitySubtitle;
    transport.inject(id, hello);

    EXPECT_EQ(transport.sent.size(), 1u);
    auto* welcome = std::get_if<WelcomeMessage>(&transport.sent[0].second);
    EXPECT_TRUE(welcome != nullptr);
    // STT 不可用 → 字幕不在服务端支持集合内，但基础 PTT 仍协商通过，连接保留
    EXPECT_EQ(welcome->serverCapabilities, static_cast<uint8_t>(CapabilityPtt));
    EXPECT_FALSE(welcome->sttEnabled);
}

TEST(server_runtime_never_negotiates_undeclared_capability) {
    FakeTransport transport;
    ServerRuntime runtime(transport, ServerConfig{});
    auto id = makePlayerId(13);

    HelloMessage hello;
    hello.playerId = id;
    hello.protocolVersion = kProtocolVersion;
    hello.capabilities = CapabilityPtt; // 未声明字幕
    transport.inject(id, hello);

    EXPECT_EQ(transport.sent.size(), 1u);
    auto* welcome = std::get_if<WelcomeMessage>(&transport.sent[0].second);
    EXPECT_TRUE(welcome != nullptr);
    EXPECT_FALSE((welcome->serverCapabilities & CapabilitySubtitle) != 0);
}
