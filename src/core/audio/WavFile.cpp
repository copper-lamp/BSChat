#include "core/audio/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace bsc::audio {
namespace {

constexpr uint16_t kFormatPcm        = 0x0001;
constexpr uint16_t kFormatFloat      = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool tagEquals(const uint8_t* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

float decodeSample(const uint8_t* p, uint16_t format, uint16_t bits) {
    if (format == kFormatFloat) {
        if (bits == 32) {
            float v = 0.0F;
            std::memcpy(&v, p, sizeof(v));
            return std::clamp(v, -1.0F, 1.0F);
        }
        if (bits == 64) {
            double v = 0.0;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(std::clamp(v, -1.0, 1.0));
        }
        return 0.0F;
    }
    switch (bits) {
    case 8:
        return (static_cast<float>(p[0]) - 128.0F) / 128.0F;
    case 16: {
        const auto v = static_cast<int16_t>(readU16(p));
        return static_cast<float>(v) / 32768.0F;
    }
    case 24: {
        int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
        if (v & 0x00800000) v |= static_cast<int32_t>(0xFF000000); // 符号扩展
        return static_cast<float>(v) / 8388608.0F;
    }
    case 32: {
        const auto v = static_cast<int32_t>(readU32(p));
        return static_cast<float>(static_cast<double>(v) / 2147483648.0);
    }
    default:
        return 0.0F;
    }
}

} // namespace

bool parseWav(const uint8_t* data, std::size_t size, WavClip& out, std::string& error) {
    out = WavClip{};
    error.clear();
    if (!data || size < 12) {
        error = "wav too small";
        return false;
    }
    if (!tagEquals(data, "RIFF") || !tagEquals(data + 8, "WAVE")) {
        error = "not a RIFF/WAVE stream";
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sampleRate = 0;
    const uint8_t* payload = nullptr;
    std::size_t payloadSize = 0;

    std::size_t cursor = 12;
    while (cursor + 8 <= size) {
        const uint8_t* chunk = data + cursor;
        const uint32_t chunkSize = readU32(chunk + 4);
        const std::size_t body = cursor + 8;
        if (body + chunkSize > size) break; // 截断块：停止解析，保留已读到的部分
        if (tagEquals(chunk, "fmt ") && chunkSize >= 16) {
            format = readU16(data + body);
            channels = readU16(data + body + 2);
            sampleRate = readU32(data + body + 4);
            bits = readU16(data + body + 14);
            if (format == kFormatExtensible && chunkSize >= 40) {
                // 子格式 GUID 的前两字节即真实格式（PCM / IEEE_FLOAT）
                format = readU16(data + body + 24);
            }
        } else if (tagEquals(chunk, "data")) {
            payload = data + body;
            payloadSize = chunkSize;
        }
        cursor = body + chunkSize + (chunkSize & 1U); // 块按偶数字节对齐
    }

    if (format != kFormatPcm && format != kFormatFloat) {
        error = "unsupported wav encoding (only PCM and IEEE float)";
        return false;
    }
    if (channels == 0 || sampleRate == 0 || bits == 0) {
        error = "wav missing fmt chunk";
        return false;
    }
    if (!payload) {
        error = "wav missing data chunk";
        return false;
    }
    const std::size_t bytesPerSample = bits / 8;
    if (bytesPerSample == 0) {
        error = "wav has invalid sample size";
        return false;
    }

    out.sampleRate = static_cast<int>(sampleRate);
    out.channels = static_cast<int>(channels);
    const std::size_t frameBytes = bytesPerSample * channels;
    const std::size_t frames = payloadSize / frameBytes;
    out.samples.resize(frames * channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = decodeSample(payload + i * bytesPerSample, format, bits);
    }
    if (frames == 0) {
        error = "wav data chunk is empty";
        return false;
    }
    return true;
}

bool loadWavFile(const std::string& path, WavClip& out, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open file: " + path;
        return false;
    }
    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    if (bytes.empty()) {
        error = "file is empty: " + path;
        return false;
    }
    return parseWav(bytes.data(), bytes.size(), out, error);
}

std::vector<float> toMonoResampled(const WavClip& clip, int targetSampleRate) {
    if (clip.sampleRate <= 0 || clip.channels <= 0 || clip.samples.empty()) return {};
    const std::size_t frames = clip.samples.size() / static_cast<std::size_t>(clip.channels);
    if (frames == 0) return {};

    std::vector<float> mono(frames, 0.0F);
    for (std::size_t i = 0; i < frames; ++i) {
        float sum = 0.0F;
        for (int c = 0; c < clip.channels; ++c) {
            sum += clip.samples[i * static_cast<std::size_t>(clip.channels) + static_cast<std::size_t>(c)];
        }
        mono[i] = sum / static_cast<float>(clip.channels);
    }

    if (targetSampleRate <= 0 || targetSampleRate == clip.sampleRate || frames < 2) return mono;

    const double ratio = static_cast<double>(targetSampleRate) / static_cast<double>(clip.sampleRate);
    const auto outFrames = static_cast<std::size_t>(std::llround(static_cast<double>(frames) * ratio));
    if (outFrames == 0) return {};

    std::vector<float> out(outFrames, 0.0F);
    for (std::size_t i = 0; i < outFrames; ++i) {
        const double src = static_cast<double>(i) / ratio;
        const auto i0 = static_cast<std::size_t>(src);
        if (i0 >= frames) {
            out[i] = 0.0F;
            continue;
        }
        const std::size_t i1 = std::min(i0 + 1, frames - 1);
        const float t = static_cast<float>(src - static_cast<double>(i0));
        out[i] = mono[i0] * (1.0F - t) + mono[i1] * t;
    }
    return out;
}

bool loadWavMono(
    const std::string&  path,
    int                 targetSampleRate,
    std::vector<float>& out,
    std::string&        error
) {
    WavClip clip;
    if (!loadWavFile(path, clip, error)) return false;
    out = toMonoResampled(clip, targetSampleRate);
    if (out.empty()) {
        error = "wav decoded to empty mono pcm: " + path;
        return false;
    }
    return true;
}

} // namespace bsc::audio
