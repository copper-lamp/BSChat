#include "server/entry/ServerRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <vector>
#include <utility>
#include <variant>

namespace bsc::server {

void ServerRuntime::logInfo(std::string const& message) const {
    if (logSink_) logSink_(false, message);
}

void ServerRuntime::logWarn(std::string const& message) const {
    if (logSink_) logSink_(true, message);
}

namespace {

ServerMixer::Config mixerConfigFrom(const config::ServerConfig& config) {
    ServerMixer::Config mixerConfig;
    mixerConfig.sampleRate = config.audio.sampleRate;
    mixerConfig.channels = config.audio.channels;
    mixerConfig.frameSizeMs = config.audio.frameSizeMs;
    mixerConfig.bitrateKbps = config.audio.bitrateKbps;
    mixerConfig.complexity = config.audio.complexity;
    mixerConfig.enableDtx = config.audio.enableDtx;
    mixerConfig.maxPending = config.maxPending;
    mixerConfig.spatial.mode = config.spatialMode == "proximity"
        ? audio::MixMode::Proximity : audio::MixMode::Global;
    mixerConfig.spatial.attenuationRadiusBlocks = config.spatialRadius;
    mixerConfig.spatial.maxAudibleRadiusBlocks = config.spatialRadius > 0.0f
        ? config.spatialRadius * 2.0f : 0.0f;
    mixerConfig.maxTalkers = config.spatialMaxTalkers;
    mixerConfig.maxChatters = config.spatialMaxChatters;
    mixerConfig.staleMs = config.spatialStaleMs;
    return mixerConfig;
}

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

ServerRuntime::ServerRuntime(pipeline::ITransport& transport, config::ServerConfig config)
: transport_(transport), config_(std::move(config)), mixer_(sessions_, mixerConfigFrom(config_)) {
    transport_.setMessageHandler([this](const protocol::PlayerId& peerId, const protocol::Message& message) {
        handleMessage(peerId, message);
    });

    stt_ = createStt();
    mixer_.setStt(stt_.get());
}

ServerRuntime::~ServerRuntime() {
    stop();
    mixer_.setStt(nullptr);
    if (stt_) stt_->shutdown();
    transport_.clearMessageHandler();
}

void ServerRuntime::start() {
    if (running_) return;
    running_ = true;
    {
        std::lock_guard lock(speechMutex_);
        speechLoggingStopped_ = false;
    }
    // ServerMod 在主线程的 ServerLevelTick 中驱动 tickOnce；不要再启动
    // 第二个音频线程，否则 mixer/encoder/STT 会被并发访问。
    // mixer_.start() 保留为独立宿主的显式能力，但本运行时不启用。
}

void ServerRuntime::stop() {
    running_ = false;
    std::vector<protocol::PlayerId> activeSpeakers;
    {
        std::lock_guard lock(speechMutex_);
        speechLoggingStopped_ = true;
        activeSpeakers.reserve(speechActivities_.size());
        for (const auto& [id, activity] : speechActivities_) {
            (void)activity;
            activeSpeakers.push_back(id);
        }
    }
    const int64_t nowMs = steadyNowMs();
    for (const auto& id : activeSpeakers) finishSpeechActivity(id, "runtime_stopped", nowMs);
    mixer_.stopFilePlayback();
    mixer_.stop();
}

bool ServerRuntime::playAudioFile(std::string const& path, std::string& error) {
    if (!config_.voiceEnabled) {
        error = "voice chat is disabled in the server config";
        return false;
    }
    if (!mixer_.startFilePlayback(path, error)) return false;
    logInfo("[audio] file playback started: " + mixer_.filePlaybackName());
    return true;
}

void ServerRuntime::stopAudioFilePlayback() {
    if (!mixer_.filePlaybackActive()) return;
    mixer_.stopFilePlayback();
    logInfo("[audio] file playback stopped");
}

bool ServerRuntime::audioFilePlaying() const { return mixer_.filePlaybackActive(); }

std::string ServerRuntime::audioFileName() const { return mixer_.filePlaybackName(); }

void ServerRuntime::tickOnce(int64_t nowMs) {
    if (!config_.voiceEnabled) return;
    mixer_.tickOnce(nowMs);
}

void ServerRuntime::drainPending() {
    mixer_.drainPending([this](const protocol::PlayerId& peerId, const protocol::Message& message) {
        if (std::holds_alternative<protocol::SttTextMessage>(message)) {
            std::lock_guard lock(negotiatedCapabilitiesMutex_);
            const auto it = negotiatedCapabilities_.find(peerId);
            if (it == negotiatedCapabilities_.end() || (it->second & protocol::CapabilitySubtitle) == 0) return;
        }
        transport_.send(peerId, message);
        if (std::holds_alternative<protocol::MixStreamMessage>(message)) {
            ++sentMixFrames_;
            if (sentMixFrames_ == 1) logInfo("[smoke] downlink = PASS (server sent first MixStream to a listener)");
        }
    });
}
void ServerRuntime::handleMessage(const protocol::PlayerId& peerId, const protocol::Message& message) {
    if (!std::holds_alternative<protocol::HelloMessage>(message) && !sessions_.find(peerId)) return;
    std::visit(
        [this, &peerId](const auto& typedMessage) {
            using MessageType = std::decay_t<decltype(typedMessage)>;
            if constexpr (std::is_same_v<MessageType, protocol::HelloMessage>) {
                handleHello(peerId, typedMessage);
            } else if constexpr (std::is_same_v<MessageType, protocol::AudioDataMessage>) {
                handleAudio(peerId, typedMessage);
            } else if constexpr (std::is_same_v<MessageType, protocol::ControlMessage>) {
                handleControl(peerId, typedMessage);
            } else if constexpr (std::is_same_v<MessageType, protocol::PosUpdateMessage>) {
                auto session = sessions_.find(peerId);
                if (session && typedMessage.playerId == peerId && std::isfinite(typedMessage.x) && std::isfinite(typedMessage.y) && std::isfinite(typedMessage.z)) {
                    session->updatePosition(typedMessage, steadyNowMs());
                }
            } else if constexpr (std::is_same_v<MessageType, protocol::UiFormMessage>) {
                if (uiFormHandler_) uiFormHandler_(peerId, typedMessage);
            }
        },
        message
    );
}
void ServerRuntime::handleHello(const protocol::PlayerId& peerId, const protocol::HelloMessage& hello) {
    if (hello.protocolVersion != protocol::kProtocolVersion || hello.playerId != peerId) return;
    if (config_.maxSessions != 0 && sessions_.size() >= config_.maxSessions && !sessions_.find(peerId)) return;
    finishSpeechActivity(peerId, "session_replaced", steadyNowMs());
    sessions_.addSession(peerId, makeSessionOptions());
    protocol::WelcomeMessage welcome;
    welcome.protocolVersion = protocol::kProtocolVersion;
    welcome.sampleRate = static_cast<uint16_t>(config_.audio.sampleRate);
    welcome.frameSizeMs = static_cast<uint8_t>(config_.audio.frameSizeMs);
    welcome.sttEnabled = config_.sttEnabled && stt_ && stt_->available();
    const uint8_t supportedCapabilities = welcome.sttEnabled ? protocol::CapabilitySubtitle : protocol::CapabilityNone;
    welcome.serverCapabilities = hello.capabilities & supportedCapabilities;
    {
        std::lock_guard lock(negotiatedCapabilitiesMutex_);
        negotiatedCapabilities_[peerId] = welcome.serverCapabilities;
    }
    transport_.send(peerId, welcome);
    logInfo(
        "[smoke] handshake = PASS (session established, sessions=" + std::to_string(sessions_.size())
        + " sampleRate=" + std::to_string(welcome.sampleRate)
        + " frameSizeMs=" + std::to_string(welcome.frameSizeMs) + ")"
    );
}
void ServerRuntime::handleAudio(const protocol::PlayerId& peerId, const protocol::AudioDataMessage& audio) {
    const int64_t nowMs = steadyNowMs();
    auto session = sessions_.find(peerId);
    if (!session) {
        recordAudioDiagnostic("session_missing", audio.opusData.size(), nowMs);
        return;
    }
    if (!config_.voiceEnabled) {
        recordAudioDiagnostic("voice_disabled", audio.opusData.size(), nowMs);
        return;
    }

    bool firstAccepted = false;
    bool hasSpeechActivity = false;
    {
        std::lock_guard lock(speechMutex_);
        if (speechLoggingStopped_) return;
        const auto result = session->pushAudio(audio, nowMs);
        if (result == PlayerSession::PushResult::Accepted || result == PlayerSession::PushResult::AcceptedWithEviction) {
            firstAccepted = ++acceptedAudioFrames_ == 1;
        }

        auto it = speechActivities_.find(peerId);
        if (it != speechActivities_.end()) {
            hasSpeechActivity = true;
            auto& activity = it->second;
            ++activity.receivedFrames;
            activity.opusBytes += audio.opusData.size();
            switch (result) {
                case PlayerSession::PushResult::Accepted:
                    ++activity.acceptedFrames;
                    break;
                case PlayerSession::PushResult::AcceptedWithEviction:
                    ++activity.acceptedFrames;
                    ++activity.evictedFrames;
                    break;
                case PlayerSession::PushResult::RateLimited:
                    ++activity.rateLimitedFrames;
                    break;
                case PlayerSession::PushResult::Late:
                    ++activity.lateFrames;
                    break;
                case PlayerSession::PushResult::Duplicate:
                    ++activity.duplicateFrames;
                    break;
                case PlayerSession::PushResult::BufferFull:
                    ++activity.bufferFullFrames;
                    break;
                case PlayerSession::PushResult::InvalidFrame:
                    ++activity.invalidFrames;
                    break;
            }
        }
    }
    if (!hasSpeechActivity) recordAudioDiagnostic("outside_ptt_activity", audio.opusData.size(), nowMs);
    if (firstAccepted) {
        logInfo("[smoke] uplink = PASS (first voice frame accepted, bytes=" + std::to_string(audio.opusData.size()) + ")");
    }
}

void ServerRuntime::handleControl(const protocol::PlayerId& peerId, const protocol::ControlMessage& control) {
    if (!sessions_.find(peerId)) return;
    const int64_t nowMs = steadyNowMs();
    if (control.type == protocol::ControlType::PttPressed) {
        std::lock_guard lock(speechMutex_);
        if (speechLoggingStopped_) return;
        if (speechActivities_.find(peerId) == speechActivities_.end()) {
            speechActivities_.emplace(peerId, SpeechActivity{nowMs});
        }
    } else if (control.type == protocol::ControlType::PttReleased) {
        finishSpeechActivity(peerId, "ptt_released", nowMs);
    }
}

void ServerRuntime::finishSpeechActivity(const protocol::PlayerId& id, std::string const& reason, int64_t nowMs) {
    SpeechActivity activity;
    {
        std::lock_guard lock(speechMutex_);
        auto it = speechActivities_.find(id);
        if (it == speechActivities_.end()) return;
        activity = it->second;
        speechActivities_.erase(it);
    }
    logInfo(formatSpeechSummary(id, activity, reason, nowMs));
}

std::string ServerRuntime::formatSpeechSummary(const protocol::PlayerId& id, SpeechActivity const& activity, std::string const& reason, int64_t nowMs) const {
    std::ostringstream speaker;
    speaker << std::hex << std::setfill('0');
    for (uint8_t byte : id) speaker << std::setw(2) << static_cast<unsigned>(byte);
    const int64_t durationMs = std::max<int64_t>(0, nowMs - activity.startedAtMs);
    const double avgFps = durationMs > 0 ? static_cast<double>(activity.receivedFrames) * 1000.0 / durationMs : 0.0;
    const uint64_t dropped = activity.rateLimitedFrames + activity.lateFrames + activity.duplicateFrames + activity.invalidFrames + activity.bufferFullFrames + activity.evictedFrames;
    const auto options = makeSessionOptions();
    std::ostringstream line;
    line << "[speech] summary speaker=" << speaker.str()
         << " reason=" << reason
         << " duration_ms=" << durationMs
         << " received_frames=" << activity.receivedFrames
         << " accepted_frames=" << activity.acceptedFrames
         << " dropped_frames=" << dropped
         << " rate_limited=" << activity.rateLimitedFrames
         << " late=" << activity.lateFrames
         << " duplicate=" << activity.duplicateFrames
         << " invalid=" << activity.invalidFrames
         << " buffer_full=" << activity.bufferFullFrames
         << " evicted_frames=" << activity.evictedFrames
         << " opus_bytes=" << activity.opusBytes
         << " avg_fps=" << std::fixed << std::setprecision(2) << avgFps
         << " max_fps=" << options.maxFramesPerSecond
         << " frame_size_ms=" << options.frameSizeMs
         << " jitter_depth_frames=" << options.maxDepthFrames;
    return line.str();
}

void ServerRuntime::recordAudioDiagnostic(std::string const& reason, size_t bytes, int64_t nowMs) {
    static const std::array<std::string, 3> reasons = {"session_missing", "voice_disabled", "outside_ptt_activity"};
    const auto reasonIt = std::find(reasons.begin(), reasons.end(), reason);
    if (reasonIt == reasons.end()) return;
    const size_t index = static_cast<size_t>(std::distance(reasons.begin(), reasonIt));
    std::string line;
    {
        std::lock_guard lock(speechMutex_);
        auto& diagnostic = audioDiagnostics_[index];
        ++diagnostic.suppressedMessages;
        diagnostic.suppressedBytes += bytes;
        if (diagnostic.lastLoggedAtMs != 0 && nowMs - diagnostic.lastLoggedAtMs < 10000) return;
        line = "[speech] audio_diagnostic reason=" + reason
            + " aggregated_messages=" + std::to_string(diagnostic.suppressedMessages)
            + " opus_bytes=" + std::to_string(diagnostic.suppressedBytes);
        diagnostic.lastLoggedAtMs = nowMs;
        diagnostic.suppressedMessages = 0;
        diagnostic.suppressedBytes = 0;
    }
    logWarn(line);
}

void ServerRuntime::removeSession(const protocol::PlayerId& id) {
    finishSpeechActivity(id, "disconnected", steadyNowMs());
    {
        std::lock_guard lock(negotiatedCapabilitiesMutex_);
        negotiatedCapabilities_.erase(id);
    }
    sessions_.removeSession(id);
}
PlayerSession::Options ServerRuntime::makeSessionOptions() const {
    PlayerSession::Options options;
    options.sampleRate = config_.audio.sampleRate;
    options.channels = config_.audio.channels;
    options.frameSizeMs = config_.audio.frameSizeMs;
    options.maxDepthFrames = static_cast<size_t>(config_.jitterMaxDepthFrames);
    options.maxWaitMs = config_.jitterMaxWaitMs;
    // 防刷包上限必须跟着帧长走：20ms 帧的正常上行是 50 帧/秒，固定 40 上限会把正常语音
    // 当洪水丢掉（听感直接崩）。这里按 2 倍标称速率设下限，60ms 帧仍是原来的 40。
    const int frameMs = std::max(1, config_.audio.frameSizeMs);
    options.maxFramesPerSecond = std::max<size_t>(40, static_cast<size_t>(1000 / frameMs) * 2);
    return options;
}

std::unique_ptr<SherpaStt> ServerRuntime::createStt() const {
    if (!config_.sttEnabled) return nullptr;

    SherpaStt::Options options;
    options.libraryPath = config_.sttModel.libraryPath;
    options.encoderPath = config_.sttModel.encoderPath;
    options.decoderPath = config_.sttModel.decoderPath;
    options.joinerPath = config_.sttModel.joinerPath;
    options.tokensPath = config_.sttModel.tokensPath;
    options.threads = config_.sttModel.threads;
    options.partialIntervalMs = config_.sttModel.partialIntervalMs;
    return std::make_unique<SherpaStt>(std::move(options));
}

} // namespace bsc::server
