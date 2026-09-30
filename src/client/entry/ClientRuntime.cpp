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
    vadEnabled_.store(config_.vadEnabled);
    talkMode_.store(config_.vadEnabled ? 1 : 0);
    // VAD 判定器的回调在本对象里驱动 setTalking；它运行在采集（音频）线程。
    // 绝对下限只当「确实是静音」的兜底（0.004 ≈ -48dBFS），真正的门限由自适应噪声底决定：
    // 早先固定 0.018/0.010 的门限在低增益麦克风上永远触发不了。
    vad_ = std::make_unique<input::VadTrigger>(0.004F, 350, 60, [this](bool active, int64_t) { setTalking(active); });
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
    talking_.store(false);
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
    talking_.store(false);
    pcmPending_ = 0;
    declaredCapabilities_ = protocol::CapabilityNone;
    negotiatedCapabilities_ = protocol::CapabilityNone;
    negotiatedSttEnabled_ = false;
    if (encoder_) encoder_->reset();
    if (decoder_) decoder_->reset();
    vadResetRequested_.store(true);
    jitter_.clear();
    sendHello();
}

void ClientRuntime::stop() {
    state_ = State::Stopped;
    talking_.store(false);
    pcmPending_ = 0;
    declaredCapabilities_ = protocol::CapabilityNone;
    negotiatedCapabilities_ = protocol::CapabilityNone;
    negotiatedSttEnabled_ = false;
    vadResetRequested_.store(true);
    jitter_.clear();
}

void ClientRuntime::applyConfig(config::ClientConfig const& config) {
    // 音频格式（采样率/声道/帧长/码率）不在此改：渲染设备在 onJoin 时按格式 Initialize，
    // 会话中换格式会让设备与编解码器错位。帧长由服务端协商决定，同样不接受面板改动。
    bool const vadBefore = vadEnabled_.load();

    config_.voiceEnabled     = config.voiceEnabled;
    config_.vadEnabled       = config.vadEnabled;
    config_.subtitleEnabled  = config.subtitleEnabled;
    config_.handshakeRetryMs = config.handshakeRetryMs;
    config_.playbackVolume   = config.playbackVolume;

    vadEnabled_.store(config_.vadEnabled);
    talkMode_.store(config_.vadEnabled ? 1 : 0);
    setOutputVolume(config_.playbackVolume);

    // 说话模式变化时让采集线程重置检测器（此处只置标志，不跨线程直接碰 vad_）：
    //  - 切回按键说话：检测器可能正把麦克风按在开启状态，而下一次 PTT 按键的 setTalking(true)
    //    会被 talking_ 相同短路掉，表现为「第一次按没反应」；
    //  - 重新打开自动检测：刚才手动说话期间采集的全是人声，噪声底若沿用会被抬到人声之上，
    //    之后一直检测不到说话，必须重新学。
    if (vadBefore != config_.vadEnabled) {
        if (vadBefore && talking_.load()) setTalking(false);
        vadResetRequested_.store(true);
    }

    // 故意不因能力位变化重新握手：CapabilityVad 是纯客户端行为（服务端不参与协商），
    // 字幕位的开关由 config_.subtitleEnabled 就地生效。重握手会打断正在进行的下行播放。
    // 声明位 declaredCapabilities_ 留给下一次自然握手刷新。
}

void ClientRuntime::sendHello() {
    protocol::HelloMessage hello;
    hello.playerId = player_.playerId();
    hello.protocolVersion = protocol::kProtocolVersion;
    hello.sampleRate = checkedSampleRate(config_.audio.sampleRate);
    hello.frameSizeMs = checkedFrameSize(config_.audio.frameSizeMs);
    hello.capabilities = protocol::CapabilityPtt
        | (vadEnabled_.load() ? protocol::CapabilityVad : 0)
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
        // 新服务端：严格按协商位；旧服务端（未协商，0）回落 sttEnabled 字段判定。
        const bool legacyServer = negotiatedCapabilities_ == protocol::kCapabilitiesNotNegotiated;
        const bool subtitleNegotiated = legacyServer
            ? negotiatedSttEnabled_
            : protocol::capabilityEnabled(negotiatedCapabilities_, protocol::CapabilitySubtitle);
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
    // 采集（音频）线程的 VAD 判定与主线程的 PTT 按键可能同时改这里，用 CAS 保证
    // 「同一次状态变化只发一条控制消息」在并发下也成立。
    if (state_ != State::Ready) return;
    if (talking_.exchange(talking) == talking) return;
    transport_.send({}, protocol::ControlMessage{
        talking ? protocol::ControlType::PttPressed : protocol::ControlType::PttReleased,
        0
    });
}

bool ClientRuntime::setPttPressed(bool down) {
    if (talkMode_.load() != 0) return false;
    setTalking(down);
    return true;
}

void ClientRuntime::submitAudio(std::vector<uint8_t> data) {
    if (state_ != State::Ready || !talking_.load() || data.empty()) return;
    transport_.send({}, protocol::AudioDataMessage{seq_++, protocol::AudioFlagNone, std::move(data)});
    ++sentAudioFrames_;
}

void ClientRuntime::submitVadPcm(const float* pcm, std::size_t samples) {
    if (!vadEnabled_.load() || !vad_ || !pcm || samples == 0) return;
    // 主线程（面板切换说话模式 / 进出世界）请求重置检测器：噪声底与迟滞计时由采集线程独占，
    // 这里消费标志而不是让对方直接改对象，避免无锁数据竞争。
    if (vadResetRequested_.exchange(false)) vad_->reset(clock_.nowMs());

    // 判定顺序很关键：先让检测器看这一帧的能量（它会通过回调调 setTalking），
    // 再看 talking_ 决定这一帧要不要编码上行。若顺序反过来，本帧就会被丢掉，
    // 表现为「自动检测触发了但第一句开头少几个字」。
    double sum = 0.0;
    for (std::size_t i = 0; i < samples; ++i) sum += static_cast<double>(pcm[i]) * pcm[i];
    const float rms = static_cast<float>(std::sqrt(sum / static_cast<double>(samples)));
    vad_->onLevel(rms, clock_.nowMs());

    if (talking_.load()) submitPcm(pcm, samples);
}

void ClientRuntime::submitPcm(const float* pcm, std::size_t samples) {
    if (state_ != State::Ready || !talking_.load() || !pcm || samples == 0 || !encoder_) return;
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
