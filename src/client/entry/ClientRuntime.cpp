#include "client/entry/ClientRuntime.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <cmath>

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
    transport_.setMessageHandler([this](const auto& id, const auto& message) { onMessage(id, message); });
    const int rate = std::max(8000, config_.audio.sampleRate);
    const int frameSamples = std::max(1, rate * std::max(1, config_.audio.frameSizeMs) / 1000);
    encoder_ = std::make_unique<codec::OpusEncoder>(rate, config_.audio.channels, frameSamples,
        config_.audio.bitrateKbps, config_.audio.complexity);
    decoder_ = std::make_unique<codec::OpusDecoder>(rate, config_.audio.channels, frameSamples);
    pcmFrame_.resize(static_cast<std::size_t>(frameSamples) * std::max(1, config_.audio.channels));
    encoded_.resize(static_cast<std::size_t>(encoder_->maxPacketSize()));
    jitter_ = ::vc::audio::JitterBuffer({static_cast<std::size_t>(std::max(1, config_.jitterMaxDepthFrames)),
                                   std::max<int64_t>(0, config_.jitterMaxWaitMs)});
}

ClientRuntime::~ClientRuntime() {
    stop();
    transport_.clearMessageHandler();
}

void ClientRuntime::start() {
    state_ = State::Handshaking;
    nextHelloMs_ = 0;
    nextPositionMs_ = 0;
    seq_ = 0;
    talking_ = false;
    pcmPending_ = 0;
    if (encoder_) encoder_->reset();
    if (decoder_) decoder_->reset();
    jitter_.clear();
    sendHello();
}

void ClientRuntime::stop() {
    state_ = State::Stopped;
    talking_ = false;
    pcmPending_ = 0;
    jitter_.clear();
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
    if (state_ == State::Ready) drainPlayback();
}

void ClientRuntime::onMessage(const protocol::PlayerId& peerId, const protocol::Message& message) {
    if (peerId != protocol::PlayerId{}) return;
    if (const auto* mix = std::get_if<protocol::MixStreamMessage>(&message)) {
        ++receivedMixFrames_;
        jitter_.push(mix->seq, mix->opusData, clock_.nowMs());
        return;
    }
    if (const auto* welcome = std::get_if<protocol::WelcomeMessage>(&message)) {
        if (welcome->protocolVersion != protocol::kProtocolVersion) {
            state_ = State::Failed;
            nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
            return;
        }
        state_ = State::Ready;
        nextPositionMs_ = 0;
        if (welcome->sampleRate != 0 && welcome->sampleRate != checkedSampleRate(config_.audio.sampleRate)) {
            state_ = State::Failed;
            nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
        }
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
    if (state_ != State::Ready || !talking_ || data.empty()) return;
    transport_.send({}, protocol::AudioDataMessage{seq_++, protocol::AudioFlagNone, std::move(data)});
    ++sentAudioFrames_;
}

void ClientRuntime::submitPcm(const float* pcm, std::size_t samples) {
    if (state_ != State::Ready || !talking_ || !pcm || samples == 0 || !encoder_) return;
    const std::size_t frame = pcmFrame_.size();
    while (samples > 0) {
        const std::size_t copy = std::min(samples, frame - pcmPending_);
        std::copy(pcm, pcm + copy, pcmFrame_.begin() + static_cast<std::ptrdiff_t>(pcmPending_));
        pcm += copy; samples -= copy; pcmPending_ += copy;
        if (pcmPending_ == frame) {
            agc_.process(pcmFrame_.data(), pcmFrame_.size());
            const int n = encoder_->encode(pcmFrame_.data(), encoded_.data(), encoded_.size());
            if (n > 0) submitAudio(std::vector<uint8_t>(encoded_.begin(), encoded_.begin() + n));
            pcmPending_ = 0;
        }
    }
}

void ClientRuntime::setRenderSink(RenderSink sink) { renderSink_ = std::move(sink); }
void ClientRuntime::setOutputVolume(float volume) { outputVolume_ = std::clamp(volume, 0.0F, 1.0F); }
void ClientRuntime::setOutputMuted(bool muted) { outputMuted_ = muted; }

void ClientRuntime::drainPlayback() {
    if (!decoder_ || !renderSink_) return;
    while (auto frame = jitter_.pop(clock_.nowMs())) {
        std::vector<float> pcm(pcmFrame_.size());
        const int n = decoder_->decode(frame->data.data(), frame->data.size(), pcm.data(), pcm.size());
        if (n < 0) continue;
        const float gain = outputMuted_ ? 0.0F : outputVolume_;
        for (int i = 0; i < n; ++i) pcm[static_cast<std::size_t>(i)] = std::clamp(pcm[static_cast<std::size_t>(i)] * gain, -1.0F, 1.0F);
        renderSink_(pcm.data(), static_cast<std::size_t>(n));
        ++playedMixFrames_;
    }
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
