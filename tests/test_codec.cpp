#include "Harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/codec/OpusCodec.h"

using namespace vc::codec;

namespace {
constexpr int kSampleRate = 48000;
constexpr int kFrameSizeMs = 60;
constexpr int kFrameSamples = kSampleRate * kFrameSizeMs / 1000; // 2880

std::vector<float> makeTone(double freq, size_t samples) {
    std::vector<float> pcm(samples);
    for (size_t i = 0; i < samples; ++i) {
        pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * freq * i / kSampleRate));
    }
    return pcm;
}

double rms(const std::vector<float>& pcm) {
    double sum = 0.0;
    for (float v : pcm) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(pcm.size()));
}

} // namespace

TEST(codec_tone_roundtrip) {
    OpusEncoder enc(kSampleRate, 1, kFrameSamples, 20);
    OpusDecoder dec(kSampleRate, 1, kFrameSamples);

    auto pcm = makeTone(440.0, kFrameSamples);
    std::vector<uint8_t> packet(enc.maxPacketSize());
    int n = enc.encode(pcm.data(), packet.data(), packet.size());
    EXPECT_TRUE(n > 0); // 非静音帧应产生有效数据

    std::vector<float> out(kFrameSamples);
    int samples = dec.decode(packet.data(), static_cast<size_t>(n), out.data(), out.size());
    EXPECT_EQ(samples, kFrameSamples);
    // Opus 有损，但 440Hz 音调应保持明显能量
    EXPECT_TRUE(rms(out) > 0.1);
}

TEST(codec_silence_dtx) {
    OpusEncoder enc(kSampleRate, 1, kFrameSamples, 20);
    std::vector<float> silence(kFrameSamples, 0.0f);
    std::vector<uint8_t> packet(enc.maxPacketSize());

    int dtxFrames = 0;
    for (int i = 0; i < 5; ++i) {
        int n = enc.encode(silence.data(), packet.data(), packet.size());
        if (n == 0) ++dtxFrames; // 编码器把 DTX 静音包（1~2 字节）映射为 0
    }
    // 前 2 帧为编码器初始状态（非空），第 3 帧起应进入 DTX
    EXPECT_TRUE(dtxFrames >= 1);
}

TEST(codec_packet_loss_concealment) {
    OpusDecoder dec(kSampleRate, 1, kFrameSamples);
    std::vector<float> out(kFrameSamples);
    // 丢包隐藏：空数据应仍输出一帧推测样本
    int samples = dec.decode(nullptr, 0, out.data(), out.size());
    EXPECT_EQ(samples, kFrameSamples);
    // 容量不足应报错
    std::vector<float> small(10);
    EXPECT_TRUE(dec.decode(nullptr, 0, small.data(), small.size()) < 0);
}

TEST(codec_decode_bad_data) {
    OpusDecoder dec(kSampleRate, 1, kFrameSamples);
    std::vector<float> out(kFrameSamples);
    std::vector<uint8_t> garbage(64, 0xFF);
    // 损坏数据不应导致崩溃；返回负值或小于帧长均可接受
    int n = dec.decode(garbage.data(), garbage.size(), out.data(), out.size());
    EXPECT_TRUE(n < 0 || n <= kFrameSamples);
}
