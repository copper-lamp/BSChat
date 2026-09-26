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
