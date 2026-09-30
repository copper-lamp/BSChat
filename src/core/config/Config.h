#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/audio/AudioTypes.h"

namespace bsc::config {

// 音频参数（客户端采集/编码、服务端解码/混音共用同一基线；服务端为权威）
struct AudioConfig {
    int sampleRate = audio::kDefaultSampleRate;
    int channels = audio::kDefaultChannels;
    int frameSizeMs = audio::kDefaultFrameSizeMs;
    int bitrateKbps = audio::kDefaultBitrateKbps;
    bool enableDtx = false;
    int complexity = 10;
};

// sherpa-onnx 在线 STT 模型。
// sherpa-onnx 的在线识别器支持多个模型族，同一时刻只能配置一个
// （见 third_party/sherpa-onnx/c-api.h 的 SherpaOnnxOnlineModelConfig）。
// modelType 决定读哪几个路径字段：
//   "transducer"     → encoderPath / decoderPath / joinerPath
//   "paraformer"     → encoderPath / decoderPath
//   "zipformer2_ctc" → modelPath（单文件）
//   "nemo_ctc"       → modelPath（单文件）
//   "t_one_ctc"      → modelPath（单文件）
// 缺省 transducer，旧配置文件无需改动。
struct SttModelConfig {
    std::string modelType = "transducer";
    std::string libraryPath;
    std::string encoderPath; // encoder.onnx（相对服务端模组目录）
    std::string decoderPath; // decoder.onnx
    std::string joinerPath;  // joiner.onnx
    std::string modelPath;   // zipformer2_ctc / nemo_ctc / t_one_ctc 的单模型文件
    std::string tokensPath;  // tokens.txt
    int threads = 4;         // 推理线程数
    int partialIntervalMs = 350; // 低延迟部分结果产出间隔（累积音频时长）
};

// 服务端配置
struct ServerConfig {
    AudioConfig audio;
    bool voiceEnabled = true;
    bool sttEnabled = false;
    SttModelConfig sttModel;
    int jitterMaxDepthFrames = 6; // 6 × 60ms = 360ms
    int64_t jitterMaxWaitMs = 200;
    std::string spatialMode = "global";
    float spatialRadius = 24.0f;
    size_t spatialMaxTalkers = 0;
    size_t spatialMaxChatters = 0;
    int64_t spatialStaleMs = 2000;
    // 高级运行保护：0 表示不限制。由运行时接入，不能替代空间混音策略。
    size_t maxSessions = 128;
    size_t maxPending = 1024;
};

// 客户端配置
struct ClientConfig {
    AudioConfig audio;
    bool voiceEnabled = true;
    uint32_t pttKey = 0x56; // 默认 V（虚拟键码）
    uint32_t settingsKey = 0x4A; // 默认 J（虚拟键码）：打开语音设置面板
    bool vadEnabled = false;
    bool captureEnabled = true;   // 采集开关：关闭后只收听
    float playbackVolume = 1.0f;  // 播放音量 0..1
    bool subtitleEnabled = true;
    int maxSubtitleLines = 4;
    int subtitleFadeMs = 5000;
    bool hudEnabled = true; // 状态覆盖层开关
    int jitterMaxDepthFrames = 6;
    int64_t jitterMaxWaitMs = 200;
    int handshakeRetryMs = 5000;
};

// JSON <-> 配置模型。解析失败/字段缺失时回退默认值（自愈式加载）。
ServerConfig serverConfigFromJson(const std::string& json);
std::string serverConfigToJson(const ServerConfig& config);
ClientConfig clientConfigFromJson(const std::string& json);
std::string clientConfigToJson(const ClientConfig& config);

} // namespace bsc::config
