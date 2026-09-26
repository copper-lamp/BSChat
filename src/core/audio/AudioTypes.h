#pragma once

#include <cstdint>
#include <vector>

namespace bsc::audio {

// 默认音频参数（质量优先基线：48kHz mono 20ms 帧、40kbps、关 DTX）。
// 服务端配置里的 audio.* 是权威值：客户端在 Welcome 时采用服务端的帧长，
// 因此调质量只需要改服务端配置（低带宽场景可降到 28kbps 或改回 60ms 帧）。
inline constexpr int kDefaultSampleRate   = 48000;
inline constexpr int kDefaultChannels     = 1;
inline constexpr int kDefaultFrameSizeMs  = 20;
inline constexpr int kDefaultBitrateKbps  = 40;
inline constexpr int kMinBitrateKbps      = 6;
inline constexpr int kMaxBitrateKbps      = 96;
// Opus 单帧最长 60ms：帧长上限，也是渲染缓冲的下限要求
inline constexpr int kMaxFrameSizeMs      = 60;
inline constexpr int kMinFrameSizeMs      = 10;

// 服务端混音调度粒度：每 tick 混出 2 帧（120ms）下行
inline constexpr int kMixTickMs           = 120;

// 计算一帧样本数
inline constexpr int samplesPerFrame(int sampleRate, int frameSizeMs) {
    return sampleRate * frameSizeMs / 1000;
}

// 帧 PCM（float [-1,1]，长 = frameSamples）
using FloatVector = std::vector<float>;

} // namespace bsc::audio
