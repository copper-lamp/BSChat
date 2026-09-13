#include "server/entry/ServerRuntime.h"

#include <chrono>
#include <utility>

namespace vc::server {

namespace {

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

ServerRuntime::ServerRuntime(pipeline::ITransport& transport, config::ServerConfig config)
: transport_(transport), config_(std::move(config)), mixer_(sessions_, {}) {
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
}

void ServerRuntime::start() {
    if (running_) return;
    running_ = true;
    if (config_.voiceEnabled) mixer_.start();
}

void ServerRuntime::stop() {
    if (!running_) return;
    running_ = false;
    mixer_.stop();
    if (stt_) stt_->shutdown();
}

void ServerRuntime::tickOnce(int64_t nowMs) {
    if (!config_.voiceEnabled) return;
    mixer_.tickOnce(nowMs);
}

void ServerRuntime::drainPending() {
    mixer_.drainPending([this](const protocol::PlayerId& peerId, const protocol::Message& message) {
        transport_.send(peerId, message);
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
            }
        },
        message
    );
}

void ServerRuntime::handleHello(const protocol::PlayerId& peerId, const protocol::HelloMessage& hello) {
    if (hello.protocolVersion != protocol::kProtocolVersion || hello.playerId != peerId) return;

    sessions_.addSession(peerId, makeSessionOptions());

    protocol::WelcomeMessage welcome;
    welcome.protocolVersion = protocol::kProtocolVersion;
    welcome.sampleRate = static_cast<uint16_t>(config_.audio.sampleRate);
    welcome.frameSizeMs = static_cast<uint8_t>(config_.audio.frameSizeMs);
    welcome.sttEnabled = config_.sttEnabled && stt_ && stt_->available();
    transport_.send(peerId, welcome);
}

void ServerRuntime::handleAudio(const protocol::PlayerId& peerId, const protocol::AudioDataMessage& audio) {
    auto session = sessions_.find(peerId);
    if (!session || !config_.voiceEnabled) return;
    session->pushAudio(audio, steadyNowMs());
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
