#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

// 单测共用的最小 RIFF/WAVE 构造器（PCM16 或 IEEE float32）。
// 仅测试使用：让 WAV 解析/回放测试不必依赖外部音频资源。

namespace bsc::test {

inline void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void appendTag(std::vector<uint8_t>& out, const char* tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
}

// floatFormat=false → PCM16，true → IEEE float32；samples 为交错样本。
inline std::vector<uint8_t> buildWavBytes(
    int sampleRate,
    int channels,
    bool floatFormat,
    const std::vector<float>& samples
) {
    const int bits = floatFormat ? 32 : 16;
    const int bytesPerSample = floatFormat ? 4 : 2;

    std::vector<uint8_t> data;
    for (float s : samples) {
        if (floatFormat) {
            uint32_t raw = 0;
            float v = s;
            std::memcpy(&raw, &v, sizeof(raw));
            appendU32(data, raw);
        } else {
            auto v = static_cast<int16_t>(s * 32767.0F);
            appendU16(data, static_cast<uint16_t>(v));
        }
    }

    std::vector<uint8_t> out;
    appendTag(out, "RIFF");
    appendU32(out, static_cast<uint32_t>(36 + data.size()));
    appendTag(out, "WAVE");
    appendTag(out, "fmt ");
    appendU32(out, 16);
    appendU16(out, static_cast<uint16_t>(floatFormat ? 3 : 1));
    appendU16(out, static_cast<uint16_t>(channels));
    appendU32(out, static_cast<uint32_t>(sampleRate));
    appendU32(out, static_cast<uint32_t>(sampleRate * channels * bytesPerSample));
    appendU16(out, static_cast<uint16_t>(channels * bytesPerSample));
    appendU16(out, static_cast<uint16_t>(bits));
    appendTag(out, "data");
    appendU32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

inline std::filesystem::path writeTempWav(const std::vector<uint8_t>& bytes, const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return path;
}

} // namespace bsc::test
