#pragma once

#include <cstdint>

namespace vc::audio {

// 默认音频参数（客户端/服务端协商基线，48kHz mono 60ms 帧）
inline constexpr int kDefaultSampleRate   = 48000;
inline constexpr int kDefaultChannels     = 1;
inline constexpr int kDefaultFrameSizeMs  = 60;
inline constexpr int kDefaultBitrateKbps  = 20;
inline constexpr int kMaxBitrateKbps      = 24;

// 服务端混音调度粒度：每 tick 混出 2 帧（120ms）下行
inline constexpr int kMixTickMs           = 120;

// 计算一帧样本数
inline constexpr int samplesPerFrame(int sampleRate, int frameSizeMs) {
    return sampleRate * frameSizeMs / 1000;
}

} // namespace vc::audio
