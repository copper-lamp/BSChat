#include "Harness.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "core/protocol/Message.h"
#include "core/protocol/MessageCodec.h"

using namespace vc::protocol;

namespace {

PlayerId makePlayerId(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

} // namespace

TEST(protocol_hello_roundtrip) {
    HelloMessage m;
    m.playerId = makePlayerId(7);
    m.protocolVersion = kProtocolVersion;
    m.sampleRate = 48000;
    m.frameSizeMs = 60;
    m.capabilities = CapabilityPtt | CapabilitySubtitle;

    auto packed = MessageCodec::pack(m, 42, 1234567);
    auto unpacked = MessageCodec::unpack(packed);
    EXPECT_TRUE(unpacked.has_value());
    auto& [info, msg] = *unpacked;
    EXPECT_EQ(info.seq, 42u);
    EXPECT_EQ(info.timestampMs, 1234567);
    EXPECT_EQ(info.version, kProtocolVersion);
    EXPECT_EQ(info.type, MessageType::Hello);

    auto& out = std::get<HelloMessage>(msg);
    EXPECT_TRUE(out.playerId == m.playerId);
    EXPECT_EQ(out.protocolVersion, kProtocolVersion);
    EXPECT_EQ(out.sampleRate, 48000);
    EXPECT_EQ(out.frameSizeMs, 60);
    EXPECT_EQ(out.capabilities, CapabilityPtt | CapabilitySubtitle);
}

TEST(protocol_audio_and_mix_roundtrip) {
    AudioDataMessage audio;
    audio.seq = 100;
    audio.flags = AudioFlagStart | AudioFlagEnd;
    audio.opusData = {1, 2, 3, 4, 5, 250, 251, 252, 253, 254, 255};

    auto packed = MessageCodec::pack(audio, 1, 0);
    auto unpacked = MessageCodec::unpack(packed);
    EXPECT_TRUE(unpacked.has_value());
    auto& [info, msg] = *unpacked;
    EXPECT_EQ(info.type, MessageType::AudioData);
    auto& outAudio = std::get<AudioDataMessage>(msg);
    EXPECT_EQ(outAudio.seq, 100u);
    EXPECT_EQ(outAudio.flags, AudioFlagStart | AudioFlagEnd);
    EXPECT_TRUE(outAudio.opusData == audio.opusData);

    MixStreamMessage mix;
    mix.seq = 55;
    mix.opusData = {9, 8, 7};
    auto packedMix = MessageCodec::pack(mix, 2, 0);
    auto unpackedMix = MessageCodec::unpack(packedMix);
    EXPECT_TRUE(unpackedMix.has_value());
    EXPECT_TRUE(std::get<MixStreamMessage>(unpackedMix->second).opusData == mix.opusData);
}

TEST(protocol_welcome_stt_control_roundtrip) {
    WelcomeMessage welcome;
    welcome.protocolVersion = kProtocolVersion;
    welcome.sampleRate = 48000;
    welcome.frameSizeMs = 60;
    welcome.sttEnabled = true;
    welcome.serverCapabilities = CapabilitySubtitle;
    auto unpackedWelcome = MessageCodec::unpack(MessageCodec::pack(welcome, 0, 0));
    EXPECT_TRUE(unpackedWelcome.has_value());
    auto& outWelcome = std::get<WelcomeMessage>(unpackedWelcome->second);
    EXPECT_TRUE(outWelcome.sttEnabled);
    EXPECT_EQ(outWelcome.serverCapabilities, CapabilitySubtitle);

    SttTextMessage stt;
    stt.speakerId = makePlayerId(3);
    stt.isFinal = false;
    stt.text = "你好 world 12345";
    auto unpackedStt = MessageCodec::unpack(MessageCodec::pack(stt, 0, 0));
    EXPECT_TRUE(unpackedStt.has_value());
    auto& outStt = std::get<SttTextMessage>(unpackedStt->second);
    EXPECT_TRUE(outStt.speakerId == stt.speakerId);
    EXPECT_FALSE(outStt.isFinal);
    EXPECT_EQ(outStt.text, stt.text);

    ControlMessage control;
    control.type = ControlType::PttReleased;
    control.value = 1;
    auto unpackedCtrl = MessageCodec::unpack(MessageCodec::pack(control, 0, 0));
    EXPECT_TRUE(unpackedCtrl.has_value());
    auto& outCtrl = std::get<ControlMessage>(unpackedCtrl->second);
    EXPECT_EQ(static_cast<uint8_t>(outCtrl.type), static_cast<uint8_t>(ControlType::PttReleased));
    EXPECT_EQ(outCtrl.value, 1u);
}

TEST(protocol_unknown_type_ignored) {
    // 手工构造信封：magic + 版本 + 未知类型 200 + seq + ts + 空 payload
    std::vector<uint8_t> buf;
    auto pushU16 = [&](uint16_t v) { buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); };
    auto pushU8 = [&](uint8_t v) { buf.push_back(v); };
    auto pushU32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) buf.push_back((v >> (8 * i)) & 0xFF);
    };
    auto pushU64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) buf.push_back((v >> (8 * i)) & 0xFF);
    };

    pushU16(kMagic);
    pushU8(kProtocolVersion);
    pushU8(200); // 未知类型
    pushU32(0);
    pushU64(0);
    pushU16(0);

    auto result = MessageCodec::unpack(buf);
    EXPECT_FALSE(result.has_value());
}

TEST(protocol_corrupted_and_bad_magic_rejected) {
    // 损坏 payload：Hello 载荷被截断
    std::vector<uint8_t> buf;
    buf.push_back(kMagic & 0xFF);
    buf.push_back((kMagic >> 8) & 0xFF);
    buf.push_back(kProtocolVersion);
    buf.push_back(static_cast<uint8_t>(MessageType::Hello));
    for (int i = 0; i < 4; ++i) buf.push_back(0); // seq
    for (int i = 0; i < 8; ++i) buf.push_back(0); // ts
    buf.push_back(5); // 声称 5 字节，实际只给 1 字节
    buf.push_back(0);
    buf.push_back(0xAB); // 仅 1 字节 payload（Hello 需要更多）
    EXPECT_FALSE(MessageCodec::unpack(buf).has_value());

    // 魔数错误
    std::vector<uint8_t> bad = {0x00, 0x00, kProtocolVersion, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_FALSE(MessageCodec::unpack(bad).has_value());

    // 空数据
    EXPECT_FALSE(MessageCodec::unpack(std::vector<uint8_t>{}).has_value());
}

TEST(protocol_pos_update_roundtrip) {
    PosUpdateMessage m;
    m.playerId = makePlayerId(9);
    m.x = 123.5f;
    m.y = -45.25f;
    m.z = 88.0f;
    m.dimensionId = -7;
    m.envFlags = EnvFlagUnderwater | EnvFlagFastMove;
    m.sendAtMs = 9876543210ull;

    auto packed = MessageCodec::pack(m, 77, 1000);
    auto unpacked = MessageCodec::unpack(packed);
    EXPECT_TRUE(unpacked.has_value());
    auto& [info, msg] = *unpacked;
    EXPECT_EQ(info.type, MessageType::PosUpdate);
    EXPECT_EQ(info.seq, 77u);

    auto& out = std::get<PosUpdateMessage>(msg);
    EXPECT_TRUE(out.playerId == m.playerId);
    EXPECT_NEAR(out.x, m.x, 1e-4);
    EXPECT_NEAR(out.y, m.y, 1e-4);
    EXPECT_NEAR(out.z, m.z, 1e-4);
    EXPECT_EQ(out.dimensionId, -7);
    EXPECT_EQ(out.envFlags, static_cast<uint8_t>(EnvFlagUnderwater | EnvFlagFastMove));
    EXPECT_EQ(out.sendAtMs, 9876543210ull);

    // 载荷截断损坏 → 越界检查返回 nullopt
    auto truncated = std::vector<uint8_t>(packed.begin(), packed.end() - 1);
    EXPECT_FALSE(MessageCodec::unpack(truncated).has_value());
}
