#include "server/mixer/ServerMixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace vc::server {
namespace {
int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

ServerMixer::ServerMixer(SessionManager& sessions, Config config)
    : sessions_(sessions),
      config_(config),
      mixer_(audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs), config_.sampleRate),
      spatialPolicy_(config_.spatial),
      encoder_(config_.sampleRate, config_.channels,
               audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs),
               config_.bitrateKbps) {}

ServerMixer::~ServerMixer() { stop(); }

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
    stt_->setResultSink([this](const pipeline::SttResult& result) {
        protocol::SttTextMessage message;
        message.speakerId = result.speakerId;
        message.isFinal = result.isFinal;
        message.text = result.text;
        enqueueToAll(message);
    });
}

void ServerMixer::tickOnce(int64_t nowMs) {
    ++tickCount_;
    auto sessions = sessions_.snapshot();
    for (const auto& session : sessions) {
        while (auto frame = session->pollFrame(nowMs)) {
            if (stt_ && stt_->available()) {
                if (frame->flags & protocol::AudioFlagStart) stt_->beginUtterance(session->id());
                if (!frame->pcm.empty()) stt_->feedAudio(session->id(), frame->pcm);
                if (frame->flags & protocol::AudioFlagEnd) stt_->endUtterance(session->id());
            }
            if (!frame->pcm.empty()) mixer_.addSpeakerFrame(session->id(), std::move(frame->pcm));
        }
    }

    for (const auto& receiver : sessions) {
        mixer_.clearGain(receiver->id());
        const auto listener = receiver->spatialState();
        size_t selected = 0;
        for (const auto& speaker : sessions) {
            if (speaker->id() == receiver->id()) continue;
            const auto source = speaker->spatialState();
            const bool fresh = source.positionKnown && nowMs >= source.updatedAtMs &&
                nowMs - source.updatedAtMs <= config_.staleMs;
            audio::SpatialGainQuery query;
            query.listener = receiver->id();
            query.speaker = speaker->id();
            query.sameDimension = listener.dimensionId == source.dimensionId;
            query.positionKnown = fresh && listener.positionKnown;
            if (query.positionKnown) {
                const float dx = listener.x - source.x;
                const float dy = listener.y - source.y;
                const float dz = listener.z - source.z;
                query.distBlocks = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            const float gain = spatialPolicy_.gain(query);
            if (gain > 0.0f && (config_.maxTalkers == 0 || selected < config_.maxTalkers)) {
                mixer_.setGain(receiver->id(), speaker->id(), gain);
                ++selected;
            }
        }
    }
    if (!mixer_.hasActiveTalker()) return;

    const int frameSamples = audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs);
    const int framesPerTick = std::max(1, config_.tickMs / config_.frameSizeMs);
    std::vector<float> mixed(static_cast<size_t>(frameSamples));
    std::vector<uint8_t> packet(static_cast<size_t>(encoder_.maxPacketSize()));
    for (const auto& receiver : sessions) {
        for (int i = 0; i < framesPerTick; ++i) {
            mixer_.computeMix(receiver->id(), mixed.data());
            const int encoded = encoder_.encode(mixed.data(), packet.data(), packet.size());
            if (encoded <= 0) continue;
            protocol::MixStreamMessage message;
            message.seq = ++mixSeq_;
            message.opusData.assign(packet.begin(), packet.begin() + encoded);
            enqueueTo(receiver->id(), message);
        }
    }
}

void ServerMixer::drainPending(
    const std::function<void(const protocol::PlayerId&, const protocol::Message&)>& sendFn) {
    if (!sendFn) return;
    std::deque<std::pair<protocol::PlayerId, protocol::Message>> batch;
    {
        std::lock_guard lock(pendingMutex_);
        batch.swap(pending_);
    }
    for (auto& item : batch) sendFn(item.first, item.second);
}

void ServerMixer::threadMain() {
    while (running_.load()) {
        const int64_t start = steadyNowMs();
        tickOnce(start);
        const int64_t sleepMs = config_.tickMs - (steadyNowMs() - start);
        if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

void ServerMixer::enqueueToAll(const protocol::Message& message) {
    for (const auto& session : sessions_.snapshot()) enqueueTo(session->id(), message);
}

void ServerMixer::enqueueTo(const protocol::PlayerId& peerId, const protocol::Message& message) {
    std::lock_guard lock(pendingMutex_);
    if (config_.maxPending == 0) return;
    if (pending_.size() >= config_.maxPending) pending_.pop_front();
    pending_.emplace_back(peerId, message);
}

} // namespace vc::server
