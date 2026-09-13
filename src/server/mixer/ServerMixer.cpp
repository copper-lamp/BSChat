#include "server/mixer/ServerMixer.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace vc::server {

namespace {

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

ServerMixer::ServerMixer(SessionManager& sessions, Config config)
: sessions_(sessions),
  config_(config),
  mixer_({config_.sampleRate, audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs)}),
  encoder_(
      config_.sampleRate,
      config_.channels,
      audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs),
      config_.bitrateKbps
  ) {}

ServerMixer::~ServerMixer() {
    stop();
}

void ServerMixer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&ServerMixer::threadMain, this);
}

void ServerMixer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void ServerMixer::setStt(pipeline::IStt* stt) {
    stt_ = stt;
    if (!stt_) return;
    // STT 结果（worker 线程）→ SttText 广播，与混音流共用待发队列
    stt_->setResultSink([this](const pipeline::SttResult& result) {
        protocol::SttTextMessage msg;
        msg.speakerId = result.speakerId;
        msg.isFinal   = result.isFinal;
        msg.text      = result.text;
        enqueueToAll(msg);
    });
}

void ServerMixer::tickOnce(int64_t nowMs) {
    ++tickCount_;

    // 1) 拉各会话解码帧 → 驱动流式 STT（begin/feed/end）+ 混音器
    for (auto& session : sessions_.snapshot()) {
        while (auto frame = session->pollFrame(nowMs)) {
            if (stt_ && stt_->available()) {
                if (frame->flags & protocol::AudioFlagStart) {
                    stt_->beginUtterance(session->id());
                }
                if (!frame->pcm.empty()) {
                    stt_->feedAudio(session->id(), frame->pcm);
                }
                if (frame->flags & protocol::AudioFlagEnd) {
                    stt_->endUtterance(session->id());
                }
            }
            if (!frame->pcm.empty()) {
                mixer_.addFrame(session->id(), std::move(frame->pcm));
            }
            // 静音帧不喂入混音器（无能量，按无该说话者处理）
        }
    }

    // 2) 无活跃发言 → 整 tick 不推流
    if (!mixer_.hasActiveTalker()) return;

    // 3) 混出本 tick 的帧（120ms = 2 × 60ms）→ 编码 → 推给所有在线会话
    const int frameSamples = audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs);
    const int framesPerTick = std::max(1, config_.tickMs / config_.frameSizeMs);
    std::vector<float> mixBuf(static_cast<size_t>(frameSamples));
    std::vector<uint8_t> packet(static_cast<size_t>(encoder_.maxPacketSize()));

    for (int i = 0; i < framesPerTick; ++i) {
        mixer_.mix(mixBuf.data());
        int n = encoder_.encode(mixBuf.data(), packet.data(), packet.size());
        if (n <= 0) continue; // DTX 判定静音 → 该帧不推

        protocol::MixStreamMessage msg;
        msg.seq = ++mixSeq_;
        msg.opusData.assign(packet.begin(), packet.begin() + n);
        enqueueToAll(msg);
    }
}

void ServerMixer::drainPending(
    const std::function<void(const protocol::PlayerId&, const protocol::Message&)>& sendFn
) {
    if (!sendFn) return;
    std::deque<std::pair<protocol::PlayerId, protocol::Message>> batch;
    {
        std::lock_guard lock(pendingMutex_);
        batch.swap(pending_);
    }
    for (auto& [peerId, message] : batch) {
        sendFn(peerId, message);
    }
}

void ServerMixer::threadMain() {
    while (running_.load()) {
        int64_t tickStart = steadyNowMs();
        tickOnce(tickStart);
        int64_t elapsed = steadyNowMs() - tickStart;
        int64_t sleepMs = config_.tickMs - elapsed;
        if (sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

void ServerMixer::enqueueToAll(const protocol::Message& message) {
    auto sessions = sessions_.snapshot();
    if (sessions.empty()) return;

    std::lock_guard lock(pendingMutex_);
    for (auto& session : sessions) {
        if (pending_.size() >= config_.maxPending) {
            pending_.pop_front(); // 背压：丢最旧，保证新鲜度
        }
        pending_.emplace_back(session->id(), message);
    }
}

} // namespace vc::server
