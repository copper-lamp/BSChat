#include "client/entry/ClientRuntime.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace vc::client {
namespace {

constexpr int64_t kPositionReportIntervalMs = 1000;

uint16_t checkedSampleRate(int value) {
    return static_cast<uint16_t>(std::clamp(value, 1, static_cast<int>(std::numeric_limits<uint16_t>::max())));
}

uint8_t checkedFrameSize(int value) {
    return static_cast<uint8_t>(std::clamp(value, 1, static_cast<int>(std::numeric_limits<uint8_t>::max())));
}

} // namespace

ClientRuntime::ClientRuntime(
    IClientTransport& transport,
    IPlayerState& player,
    IClock& clock,
    config::ClientConfig config
)
    : transport_(transport), player_(player), clock_(clock), config_(std::move(config)) {
    transport_.setMessageHandler([this](const auto& id, const auto& message) {
        onMessage(id, message);
    });
}

void ClientRuntime::start() {
    state_ = State::Handshaking;
    nextHelloMs_ = 0;
    nextPositionMs_ = 0;
    seq_ = 0;
    talking_ = false;
    sendHello();
}

void ClientRuntime::stop() {
    state_ = State::Stopped;
    talking_ = false;
}

void ClientRuntime::sendHello() {
    protocol::HelloMessage hello;
    hello.playerId = player_.playerId();
    hello.protocolVersion = protocol::kProtocolVersion;
    hello.sampleRate = checkedSampleRate(config_.audio.sampleRate);
    hello.frameSizeMs = checkedFrameSize(config_.audio.frameSizeMs);
    hello.capabilities = protocol::CapabilityPtt
        | (config_.vadEnabled ? protocol::CapabilityVad : 0)
        | (config_.subtitleEnabled ? protocol::CapabilitySubtitle : 0);
    transport_.send({}, hello);
    nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
}

void ClientRuntime::tick() {
    const auto now = clock_.nowMs();
    if ((state_ == State::Handshaking || state_ == State::Failed) && now >= nextHelloMs_) {
        state_ = State::Handshaking;
        sendHello();
    }
    if (state_ == State::Ready && now >= nextPositionMs_) {
        reportPosition();
        nextPositionMs_ = now + kPositionReportIntervalMs;
    }
}

void ClientRuntime::onMessage(const protocol::PlayerId& peerId, const protocol::Message& message) {
    if (peerId != protocol::PlayerId{}) return;
    if (const auto* welcome = std::get_if<protocol::WelcomeMessage>(&message)) {
        if (welcome->protocolVersion != protocol::kProtocolVersion) {
            state_ = State::Failed;
            nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
            return;
        }
        state_ = State::Ready;
        nextPositionMs_ = 0;
    }
}

void ClientRuntime::setTalking(bool talking) {
    if (talking == talking_ || state_ != State::Ready) return;
    talking_ = talking;
    transport_.send({}, protocol::ControlMessage{
        talking ? protocol::ControlType::PttPressed : protocol::ControlType::PttReleased,
        0
    });
}

void ClientRuntime::submitAudio(std::vector<uint8_t> data) {
    if (state_ != State::Ready || !talking_) return;
    transport_.send({}, protocol::AudioDataMessage{seq_++, protocol::AudioFlagNone, std::move(data)});
}

void ClientRuntime::reportPosition() {
    const auto position = player_.position();
    protocol::PosUpdateMessage update;
    update.playerId = player_.playerId();
    update.x = position.x;
    update.y = position.y;
    update.z = position.z;
    update.dimensionId = position.dimensionId;
    update.envFlags = position.envFlags;
    update.sendAtMs = static_cast<uint64_t>(std::max<int64_t>(0, clock_.nowMs()));
    transport_.send({}, update);
}

} // namespace vc::client
