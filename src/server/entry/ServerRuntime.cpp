#include "server/entry/ServerRuntime.h"

#include <chrono>
#include <cmath>
#include <type_traits>
#include <utility>
#include <variant>

namespace vc::server {

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
    // ServerMod 在主线程的 ServerLevelTick 中驱动 tickOnce；不要再启动
    // 第二个音频线程，否则 mixer/encoder/STT 会被并发访问。
    // mixer_.start() 保留为独立宿主的显式能力，但本运行时不启用。
}

void ServerRuntime::stop() {
    if (!running_) return;
    running_ = false;
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
        transport_.send(peerId, message);
        if (std::holds_alternative<protocol::MixStreamMessage>(message)) {
            ++sentMixFrames_;
            if (sentMixFrames_ == 1) {
                logInfo("[smoke] downlink = PASS (server sent first MixStream to a listener)");
            }
        }
    });
}

void ServerRuntime::handleMessage(const protocol::PlayerId& peerId, const protocol::Message& message) {
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

    if (config_.maxSessions != 0 && sessions_.size() >= config_.maxSessions && !sessions_.find(peerId)) {
        return;
    }
    sessions_.addSession(peerId, makeSessionOptions());

    protocol::WelcomeMessage welcome;
    welcome.protocolVersion = protocol::kProtocolVersion;
    welcome.sampleRate = static_cast<uint16_t>(config_.audio.sampleRate);
    welcome.frameSizeMs = static_cast<uint8_t>(config_.audio.frameSizeMs);
    welcome.sttEnabled = config_.sttEnabled && stt_ && stt_->available();
    transport_.send(peerId, welcome);

    logInfo(
        "[smoke] handshake = PASS (session established, sessions=" + std::to_string(sessions_.size())
        + " sampleRate=" + std::to_string(welcome.sampleRate)
        + " frameSizeMs=" + std::to_string(welcome.frameSizeMs) + ")"
    );
}
void ServerRuntime::handleAudio(const protocol::PlayerId& peerId, const protocol::AudioDataMessage& audio) {
    auto session = sessions_.find(peerId);
    if (!session || !config_.voiceEnabled) {
        logWarn(
            "[smoke] uplink rejected: session=" + std::string(session ? "true" : "false")
            + " voiceEnabled=" + std::string(config_.voiceEnabled ? "true" : "false")
        );
        return;
    }
    const size_t pendingBefore = session->pendingFrames();
    session->pushAudio(audio, steadyNowMs());
    const size_t pendingAfter = session->pendingFrames();
    if (pendingAfter > pendingBefore) {
        ++acceptedAudioFrames_;
        if (acceptedAudioFrames_ == 1) {
            logInfo(
                "[smoke] uplink = PASS (first voice frame accepted, bytes="
                + std::to_string(audio.opusData.size()) + ")"
            );
        }
    } else {
        logWarn("[smoke] uplink frame dropped by session (rate limit or invalid frame)");
    }
}

void ServerRuntime::handleControl(const protocol::PlayerId& /*peerId*/, const protocol::ControlMessage& /*control*/) {
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

} // namespace vc::server
