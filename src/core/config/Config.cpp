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

SttModelConfig sttModelFromJson(const nlohmann::json& j) {
    SttModelConfig c;
    if (j.is_object()) {
        c.libraryPath = valueOr(j, "libraryPath", c.libraryPath);
        c.encoderPath = valueOr(j, "encoderPath", c.encoderPath);
        c.decoderPath = valueOr(j, "decoderPath", c.decoderPath);
        c.joinerPath = valueOr(j, "joinerPath", c.joinerPath);
        c.tokensPath = valueOr(j, "tokensPath", c.tokensPath);
        c.threads = valueOr(j, "threads", c.threads);
        c.partialIntervalMs = valueOr(j, "partialIntervalMs", c.partialIntervalMs);
    }
    return c;
}

nlohmann::json sttModelToJson(const SttModelConfig& c) {
    nlohmann::json j;
    j["libraryPath"] = c.libraryPath;
    j["encoderPath"] = c.encoderPath;
    j["decoderPath"] = c.decoderPath;
    j["joinerPath"] = c.joinerPath;
    j["tokensPath"] = c.tokensPath;
    j["threads"] = c.threads;
    j["partialIntervalMs"] = c.partialIntervalMs;
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
        c.sttModel = sttModelFromJson(valueOr(j, "sttModel", nlohmann::json::object()));
        c.jitterMaxDepthFrames = valueOr(j, "jitterMaxDepthFrames", c.jitterMaxDepthFrames);
        c.jitterMaxWaitMs = valueOr(j, "jitterMaxWaitMs", c.jitterMaxWaitMs);
        c.spatialMode = valueOr(j, "mode", c.spatialMode);
        c.spatialRadius = valueOr(j, "radius", c.spatialRadius);
        c.spatialMaxTalkers = valueOr(j, "maxTalkers", c.spatialMaxTalkers);
        c.spatialMaxChatters = valueOr(j, "maxChatters", c.spatialMaxChatters);
        c.spatialStaleMs = valueOr(j, "staleMs", c.spatialStaleMs);
        c.maxSessions = valueOr(j, "maxSessions", c.maxSessions);
        c.maxPending = valueOr(j, "maxPending", c.maxPending);
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
    j["sttModel"] = sttModelToJson(c.sttModel);
    j["jitterMaxDepthFrames"] = c.jitterMaxDepthFrames;
    j["jitterMaxWaitMs"] = c.jitterMaxWaitMs;
    j["mode"] = c.spatialMode;
    j["radius"] = c.spatialRadius;
    j["maxTalkers"] = c.spatialMaxTalkers;
    j["maxChatters"] = c.spatialMaxChatters;
    j["staleMs"] = c.spatialStaleMs;
    j["maxSessions"] = c.maxSessions;
    j["maxPending"] = c.maxPending;
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
