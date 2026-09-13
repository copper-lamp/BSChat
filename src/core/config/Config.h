#pragma once

#include <cstdint>
#include <string>

#include "core/audio/AudioTypes.h"

namespace vc::config {

// 音频参数（客户端采集/编码、服务端解码/混音共用同一基线）
struct AudioConfig {
    int sampleRate = audio::kDefaultSampleRate;
    int channels = audio::kDefaultChannels;
    int frameSizeMs = audio::kDefaultFrameSizeMs;
    int bitrateKbps = audio::kDefaultBitrateKbps;
    bool enableDtx = true;
    int complexity = 10;
};

// 服务端配置
struct ServerConfig {
    AudioConfig audio;
    bool voiceEnabled = true;
    bool sttEnabled = false;
    std::string whisperModelPath; // 相对服务端模组目录
    int sttMaxConcurrency = 1;
    int jitterMaxDepthFrames = 6; // 6 × 60ms = 360ms
    int64_t jitterMaxWaitMs = 200;
};

// 客户端配置
struct ClientConfig {
    AudioConfig audio;
    bool voiceEnabled = true;
    uint32_t pttKey = 0x56; // 默认 V（虚拟键码）
    bool vadEnabled = false;
    bool subtitleEnabled = true;
    int maxSubtitleLines = 4;
    int subtitleFadeMs = 5000;
    int jitterMaxDepthFrames = 6;
    int64_t jitterMaxWaitMs = 200;
    int handshakeRetryMs = 5000;
};

// JSON <-> 配置模型。解析失败/字段缺失时回退默认值（自愈式加载）。
ServerConfig serverConfigFromJson(const std::string& json);
std::string serverConfigToJson(const ServerConfig& config);
ClientConfig clientConfigFromJson(const std::string& json);
std::string clientConfigToJson(const ClientConfig& config);

} // namespace vc::config
