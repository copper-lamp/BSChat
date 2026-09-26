#include "client/entry/ClientRuntime.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <cmath>

namespace bsc::client {
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
    rebuildCodecs();
    jitter_ = ::bsc::audio::JitterBuffer({static_cast<std::size_t>(std::max(1, config_.jitterMaxDepthFrames)),
                                   std::max<int64_t>(0, config_.jitterMaxWaitMs)});
}

void ClientRuntime::rebuildCodecs() {
    const int rate = std::max(8000, config_.audio.sampleRate);
    const int frameSamples = std::max(1, rate * std::max(1, config_.audio.frameSizeMs) / 1000);
    encoder_ = std::make_unique<codec::OpusEncoder>(rate, config_.audio.channels, frameSamples,
        config_.audio.bitrateKbps, config_.audio.complexity, config_.audio.enableDtx);
    decoder_ = std::make_unique<codec::OpusDecoder>(rate, config_.audio.channels, frameSamples);
    pcmFrame_.assign(static_cast<std::size_t>(frameSamples) * std::max(1, config_.audio.channels), 0.0F);
    encoded_.assign(static_cast<std::size_t>(encoder_->maxPacketSize()), 0);
    pcmPending_ = 0;
}

void ClientRuntime::applyNegotiatedFrameSize(int frameSizeMs) {
    const int clamped = std::clamp(frameSizeMs, ::bsc::audio::kMinFrameSizeMs, ::bsc::audio::kMaxFrameSizeMs);
    if (clamped == config_.audio.frameSizeMs) return;
    config_.audio.frameSizeMs = clamped;
    rebuildCodecs();
    jitter_.clear();
    talking_ = false;
    seq_ = 0;
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
    declaredCapabilities_ = protocol::CapabilityNone;
    negotiatedCapabilities_ = protocol::CapabilityNone;
    negotiatedSttEnabled_ = false;
    if (encoder_) encoder_->reset();
    if (decoder_) decoder_->reset();
    jitter_.clear();
    sendHello();
}

void ClientRuntime::stop() {
    state_ = State::Stopped;
    talking_ = false;
    pcmPending_ = 0;
    declaredCapabilities_ = protocol::CapabilityNone;
    negotiatedCapabilities_ = protocol::CapabilityNone;
    negotiatedSttEnabled_ = false;
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
    declaredCapabilities_ = hello.capabilities;
    transport_.send({}, hello);
    nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
}

void ClientRuntime::tick() {
    const auto now = clock_.nowMs();
    // 自检请求来自网络线程，这里（主线程）再取用，避免跨线程驱动运行时/日志。
    if (smokeTestRequested_.exchange(false) && smokeTestRequestHandler_) smokeTestRequestHandler_();
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
    const auto* welcome = std::get_if<protocol::WelcomeMessage>(&message);
    if (welcome) {
        if (state_ != State::Handshaking) return;
    } else if (state_ != State::Ready) {
        return;
    }
    if (const auto* control = std::get_if<protocol::ControlMessage>(&message)) {
        if (control->type == protocol::ControlType::SmokeTest) smokeTestRequested_.store(true);
        return;
    }
    if (const auto* stt = std::get_if<protocol::SttTextMessage>(&message)) {
        const bool subtitleNegotiated = (negotiatedCapabilities_ & protocol::CapabilitySubtitle) != 0
            || (negotiatedCapabilities_ == protocol::CapabilityNone && negotiatedSttEnabled_);
        if (config_.subtitleEnabled && (declaredCapabilities_ & protocol::CapabilitySubtitle) != 0
            && subtitleNegotiated && sttTextHandler_) sttTextHandler_(*stt);
        return;
    }
    if (const auto* ui = std::get_if<protocol::UiFormMessage>(&message)) {
        if (uiFormHandler_) uiFormHandler_(*ui);
        return;
    }
    if (const auto* mix = std::get_if<protocol::MixStreamMessage>(&message)) {
        ++receivedMixFrames_;
        jitter_.push(mix->seq, mix->opusData, clock_.nowMs());
        return;
    }
    if (welcome) {
        if (welcome->protocolVersion != protocol::kProtocolVersion) {
            negotiatedCapabilities_ = protocol::CapabilityNone;
            negotiatedSttEnabled_ = false;
            state_ = State::Failed;
            nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
            return;
        }
        // 采样率必须两端一致（渲染设备格式在启动时已固定，会话中无法切换），不一致判负重试；
        // 帧长以服务端为准：客户端直接采用，避免两端手填不一致导致分包/解码错位。
        if (welcome->sampleRate != 0 && welcome->sampleRate != checkedSampleRate(config_.audio.sampleRate)) {
            negotiatedCapabilities_ = protocol::CapabilityNone;
            negotiatedSttEnabled_ = false;
            state_ = State::Failed;
            nextHelloMs_ = clock_.nowMs() + std::max<int64_t>(1, config_.handshakeRetryMs);
            return;
        }
        if (welcome->frameSizeMs != 0) applyNegotiatedFrameSize(checkedFrameSize(welcome->frameSizeMs));
        negotiatedCapabilities_ = welcome->serverCapabilities & declaredCapabilities_;
        negotiatedSttEnabled_ = welcome->sttEnabled;
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
void ClientRuntime::playLocalPcm(const float* samples, std::size_t count) {
    if (renderSink_ && samples && count > 0) renderSink_(samples, count);
}
void ClientRuntime::setOutputVolume(float volume) { outputVolume_ = std::clamp(volume, 0.0F, 1.0F); }
void ClientRuntime::setOutputMuted(bool muted) { outputMuted_ = muted; }
void ClientRuntime::setSmokeTestRequestHandler(SmokeTestRequestHandler handler) {
    smokeTestRequestHandler_ = std::move(handler);
}

void ClientRuntime::setSttTextHandler(SttTextHandler handler) { sttTextHandler_ = std::move(handler); }

void ClientRuntime::setUiFormHandler(UiFormHandler handler) { uiFormHandler_ = std::move(handler); }

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

} // namespace bsc::client
