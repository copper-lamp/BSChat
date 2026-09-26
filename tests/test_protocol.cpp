#include "Harness.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "core/protocol/Message.h"
#include "core/protocol/MessageCodec.h"

using namespace bsc::protocol;

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

TEST(protocol_ui_form_roundtrip) {
    UiFormMessage request;
    request.kind = UiFormKind::Request;
    request.requestId = 4242;
    request.cancelReason = -1;
    request.payload = "{\"type\":\"custom_form\",\"title\":\"bschat\"}";

    auto unpacked = MessageCodec::unpack(MessageCodec::pack(request, 11, 2222));
    EXPECT_TRUE(unpacked.has_value());
    auto& [info, msg] = *unpacked;
    EXPECT_EQ(info.type, MessageType::UiForm);
    EXPECT_EQ(info.seq, 11u);
    auto& out = std::get<UiFormMessage>(msg);
    EXPECT_EQ(static_cast<uint8_t>(out.kind), static_cast<uint8_t>(UiFormKind::Request));
    EXPECT_EQ(out.requestId, 4242u);
    EXPECT_EQ(out.cancelReason, -1);
    EXPECT_EQ(out.payload, request.payload);

    UiFormMessage response;
    response.kind = UiFormKind::Response;
    response.requestId = 4242;
    response.cancelReason = 2;
    response.payload = "[true,1]";
    auto unpackedResponse = MessageCodec::unpack(MessageCodec::pack(response, 12, 0));
    EXPECT_TRUE(unpackedResponse.has_value());
    auto& outResponse = std::get<UiFormMessage>(unpackedResponse->second);
    EXPECT_EQ(static_cast<uint8_t>(outResponse.kind), static_cast<uint8_t>(UiFormKind::Response));
    EXPECT_EQ(outResponse.requestId, 4242u);
    EXPECT_EQ(outResponse.cancelReason, 2);
    EXPECT_EQ(outResponse.payload, response.payload);

    // 取消场景：payload 为空也要能正常往返
    UiFormMessage cancelled;
    cancelled.kind = UiFormKind::Response;
    cancelled.requestId = 9;
    cancelled.cancelReason = 0;
    auto unpackedCancelled = MessageCodec::unpack(MessageCodec::pack(cancelled, 13, 0));
    EXPECT_TRUE(unpackedCancelled.has_value());
    EXPECT_EQ(std::get<UiFormMessage>(unpackedCancelled->second).payload, std::string{});

    // 载荷截断损坏 → 越界检查返回 nullopt
    auto packed = MessageCodec::pack(request, 11, 2222);
    auto truncated = std::vector<uint8_t>(packed.begin(), packed.end() - 1);
    EXPECT_FALSE(MessageCodec::unpack(truncated).has_value());

    // 超出上限的载荷被拒绝：打包侧不产出载荷，解包侧同样不接受
    UiFormMessage oversized;
    oversized.kind = UiFormKind::Request;
    oversized.payload.assign(kUiFormPayloadMaxBytes + 1, 'x');
    EXPECT_FALSE(MessageCodec::unpack(MessageCodec::pack(oversized, 14, 0)).has_value());
}

// 信封中 payload 长度字段的偏移：magic(2) + version(1) + type(1) + seq(4) + timestamp(8)
constexpr std::size_t kEnvelopePayloadLenOffset = 2 + 1 + 1 + 4 + 8;

// v0.1.0 已发布的 v1 wire 布局固定样本。协议编码一旦改动，本用例必须先失败，
// 以便确认是否破坏与已发布旧客户端/旧服务端的互通（见 docs/bschat-protocol-compatibility-design.md）。
TEST(protocol_v1_wire_fixture_hello_is_stable) {
    const std::vector<uint8_t> fixture = {
        0x53, 0x42,                                     // 魔数 "BS"
        kProtocolVersion,                               // 信封版本
        static_cast<uint8_t>(MessageType::Hello),
        0x00, 0x00, 0x00, 0x00,                         // seq = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // timestamp = 0
        0x15, 0x00,                                     // payload 长度 = 21
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // playerId[0..7]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // playerId[8..15]
        kProtocolVersion,                               // Hello.protocolVersion
        0x80, 0xBB,                                     // sampleRate = 48000
        0x3C,                                           // frameSizeMs = 60
        0x05,                                           // capabilities = Ptt | Subtitle
    };

    auto unpacked = MessageCodec::unpack(fixture);
    EXPECT_TRUE(unpacked.has_value());
    if (!unpacked) return;
    auto& [info, msg] = *unpacked;
    EXPECT_EQ(info.type, MessageType::Hello);
    EXPECT_EQ(info.version, kProtocolVersion);
    auto& hello = std::get<HelloMessage>(msg);
    EXPECT_TRUE(hello.playerId == makePlayerId(7));
    EXPECT_EQ(hello.protocolVersion, kProtocolVersion);
    EXPECT_EQ(hello.sampleRate, 48000);
    EXPECT_EQ(hello.frameSizeMs, 60);
    EXPECT_EQ(hello.capabilities, static_cast<uint8_t>(CapabilityPtt | CapabilitySubtitle));

    // 重新打包必须逐字节复现该样本，保证新端发往旧端的字节不变。
    HelloMessage rebuilt;
    rebuilt.playerId = makePlayerId(7);
    rebuilt.protocolVersion = kProtocolVersion;
    rebuilt.sampleRate = 48000;
    rebuilt.frameSizeMs = 60;
    rebuilt.capabilities = CapabilityPtt | CapabilitySubtitle;
    EXPECT_TRUE(MessageCodec::pack(rebuilt, 0, 0) == fixture);
}

TEST(protocol_v1_wire_fixture_welcome_is_stable) {
    const std::vector<uint8_t> fixture = {
        0x53, 0x42,
        kProtocolVersion,
        static_cast<uint8_t>(MessageType::Welcome),
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x06, 0x00,             // payload 长度 = 6
        kProtocolVersion,       // Welcome.protocolVersion
        0x80, 0xBB,             // sampleRate = 48000
        0x3C,                   // frameSizeMs = 60
        0x01,                   // sttEnabled = true
        0x04,                   // serverCapabilities = Subtitle
    };

    auto unpacked = MessageCodec::unpack(fixture);
    EXPECT_TRUE(unpacked.has_value());
    if (!unpacked) return;
    EXPECT_EQ(unpacked->first.type, MessageType::Welcome);
    auto& welcome = std::get<WelcomeMessage>(unpacked->second);
    EXPECT_EQ(welcome.protocolVersion, kProtocolVersion);
    EXPECT_EQ(welcome.sampleRate, 48000);
    EXPECT_EQ(welcome.frameSizeMs, 60);
    EXPECT_TRUE(welcome.sttEnabled);
    EXPECT_EQ(welcome.serverCapabilities, static_cast<uint8_t>(CapabilitySubtitle));

    WelcomeMessage rebuilt;
    rebuilt.protocolVersion = kProtocolVersion;
    rebuilt.sampleRate = 48000;
    rebuilt.frameSizeMs = 60;
    rebuilt.sttEnabled = true;
    rebuilt.serverCapabilities = CapabilitySubtitle;
    EXPECT_TRUE(MessageCodec::pack(rebuilt, 0, 0) == fixture);
}

// 前向兼容边界：信封级尾随字节必须拒收；载荷级可选扩展（长度同步扩大）应被忽略并保留既有字段。
TEST(protocol_v1_tail_extension_and_envelope_trailing_data) {
    WelcomeMessage welcome;
    welcome.protocolVersion = kProtocolVersion;
    welcome.sampleRate = 48000;
    welcome.frameSizeMs = 60;
    welcome.sttEnabled = true;
    welcome.serverCapabilities = CapabilityPtt | CapabilitySubtitle;
    const auto packed = MessageCodec::pack(welcome, 0, 0);

    // (1) 信封级尾随字节（未计入 payload 长度）→ 拒收，避免错位误读
    auto envelopeGarbage = packed;
    envelopeGarbage.push_back(0x7F);
    EXPECT_FALSE(MessageCodec::unpack(envelopeGarbage).has_value());

    // (2) 载荷级可选扩展：长度一并扩大后，未知尾随字节被忽略，既有字段原样保留
    auto withTail = packed;
    const uint16_t payloadLen = static_cast<uint16_t>(withTail[kEnvelopePayloadLenOffset]
        | (withTail[kEnvelopePayloadLenOffset + 1] << 8));
    const uint16_t extended = static_cast<uint16_t>(payloadLen + 3);
    withTail[kEnvelopePayloadLenOffset] = static_cast<uint8_t>(extended & 0xFF);
    withTail[kEnvelopePayloadLenOffset + 1] = static_cast<uint8_t>((extended >> 8) & 0xFF);
    withTail.insert(withTail.end(), {0x7F, 0x01, 0x00});

    auto unpacked = MessageCodec::unpack(withTail);
    EXPECT_TRUE(unpacked.has_value());
    if (!unpacked) return;
    auto& out = std::get<WelcomeMessage>(unpacked->second);
    EXPECT_EQ(out.serverCapabilities, static_cast<uint8_t>(CapabilityPtt | CapabilitySubtitle));
    EXPECT_TRUE(out.sttEnabled);
    EXPECT_EQ(out.frameSizeMs, 60);
}
