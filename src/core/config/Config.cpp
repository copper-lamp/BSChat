#include "core/config/Config.h"

#include <nlohmann/json.hpp>

namespace vc::config {
namespace {

template <typename T>
T valueOr(const nlohmann::json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        try {
            return it->get<T>();
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

AudioConfig audioFromJson(const nlohmann::json& j) {
    AudioConfig c;
    if (j.is_object()) {
        c.sampleRate = valueOr(j, "sampleRate", c.sampleRate);
        c.channels = valueOr(j, "channels", c.channels);
        c.frameSizeMs = valueOr(j, "frameSizeMs", c.frameSizeMs);
        c.bitrateKbps = valueOr(j, "bitrateKbps", c.bitrateKbps);
        c.enableDtx = valueOr(j, "enableDtx", c.enableDtx);
        c.complexity = valueOr(j, "complexity", c.complexity);
    }
    return c;
}

nlohmann::json audioToJson(const AudioConfig& c) {
    nlohmann::json j;
    j["sampleRate"] = c.sampleRate;
    j["channels"] = c.channels;
    j["frameSizeMs"] = c.frameSizeMs;
    j["bitrateKbps"] = c.bitrateKbps;
    j["enableDtx"] = c.enableDtx;
    j["complexity"] = c.complexity;
    return j;
}

} // namespace

ServerConfig serverConfigFromJson(const std::string& json) {
    ServerConfig c;
    try {
        auto j = nlohmann::json::parse(json);
        c.audio = audioFromJson(valueOr(j, "audio", nlohmann::json::object()));
        c.voiceEnabled = valueOr(j, "voiceEnabled", c.voiceEnabled);
        c.sttEnabled = valueOr(j, "sttEnabled", c.sttEnabled);
        c.whisperModelPath = valueOr(j, "whisperModelPath", c.whisperModelPath);
        c.sttMaxConcurrency = valueOr(j, "sttMaxConcurrency", c.sttMaxConcurrency);
        c.jitterMaxDepthFrames = valueOr(j, "jitterMaxDepthFrames", c.jitterMaxDepthFrames);
        c.jitterMaxWaitMs = valueOr(j, "jitterMaxWaitMs", c.jitterMaxWaitMs);
    } catch (...) {
        // 解析失败 → 全部默认值
    }
    return c;
}

std::string serverConfigToJson(const ServerConfig& c) {
    nlohmann::json j;
    j["audio"] = audioToJson(c.audio);
    j["voiceEnabled"] = c.voiceEnabled;
    j["sttEnabled"] = c.sttEnabled;
    j["whisperModelPath"] = c.whisperModelPath;
    j["sttMaxConcurrency"] = c.sttMaxConcurrency;
    j["jitterMaxDepthFrames"] = c.jitterMaxDepthFrames;
    j["jitterMaxWaitMs"] = c.jitterMaxWaitMs;
    return j.dump(4);
}

ClientConfig clientConfigFromJson(const std::string& json) {
    ClientConfig c;
    try {
        auto j = nlohmann::json::parse(json);
        c.audio = audioFromJson(valueOr(j, "audio", nlohmann::json::object()));
        c.voiceEnabled = valueOr(j, "voiceEnabled", c.voiceEnabled);
        c.pttKey = valueOr(j, "pttKey", c.pttKey);
        c.vadEnabled = valueOr(j, "vadEnabled", c.vadEnabled);
        c.subtitleEnabled = valueOr(j, "subtitleEnabled", c.subtitleEnabled);
        c.maxSubtitleLines = valueOr(j, "maxSubtitleLines", c.maxSubtitleLines);
        c.subtitleFadeMs = valueOr(j, "subtitleFadeMs", c.subtitleFadeMs);
        c.jitterMaxDepthFrames = valueOr(j, "jitterMaxDepthFrames", c.jitterMaxDepthFrames);
        c.jitterMaxWaitMs = valueOr(j, "jitterMaxWaitMs", c.jitterMaxWaitMs);
        c.handshakeRetryMs = valueOr(j, "handshakeRetryMs", c.handshakeRetryMs);
    } catch (...) {
        // 解析失败 → 全部默认值
    }
    return c;
}

std::string clientConfigToJson(const ClientConfig& c) {
    nlohmann::json j;
    j["audio"] = audioToJson(c.audio);
    j["voiceEnabled"] = c.voiceEnabled;
    j["pttKey"] = c.pttKey;
    j["vadEnabled"] = c.vadEnabled;
    j["subtitleEnabled"] = c.subtitleEnabled;
    j["maxSubtitleLines"] = c.maxSubtitleLines;
    j["subtitleFadeMs"] = c.subtitleFadeMs;
    j["jitterMaxDepthFrames"] = c.jitterMaxDepthFrames;
    j["jitterMaxWaitMs"] = c.jitterMaxWaitMs;
    j["handshakeRetryMs"] = c.handshakeRetryMs;
    return j.dump(4);
}

} // namespace vc::config
