#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bsc::audio {

// 解码后的 WAV 内容（交错浮点，范围 [-1,1]）。
struct WavClip {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;
};

// 解析 RIFF/WAVE 字节流。支持 PCM 8/16/24/32-bit、IEEE float 32/64-bit 以及
// WAVE_FORMAT_EXTENSIBLE 包装（取子格式）。只取第一个 fmt/data 块，其余块跳过。
// 失败返回 false 并写 error；成功保证 out.sampleRate > 0、out.channels > 0。
bool parseWav(const uint8_t* data, std::size_t size, WavClip& out, std::string& error);

// 读取 WAV 文件（内部用 parseWav）。
bool loadWavFile(const std::string& path, WavClip& out, std::string& error);

// 交错多声道下混为单声道，并重采样到 targetSampleRate。
// targetSampleRate <= 0 或与原采样率相同时只做下混。重采样为线性插值：
// 用于测试音频回放，不作为高质量重采样器。
std::vector<float> toMonoResampled(const WavClip& clip, int targetSampleRate);

// 便捷组合：读文件 → 下混单声道 → 重采样到 targetSampleRate。
bool loadWavMono(
    const std::string& path,
    int                targetSampleRate,
    std::vector<float>& out,
    std::string&        error
);

} // namespace bsc::audio
