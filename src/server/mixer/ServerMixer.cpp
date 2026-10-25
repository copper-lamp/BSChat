#include "server/mixer/ServerMixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace bsc::server {
namespace {
int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

float frameRms(const std::vector<float>& pcm) {
    if (pcm.empty()) return 0.0f;
    double sum = 0.0;
    for (float sample : pcm) sum += static_cast<double>(sample) * sample;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(pcm.size())));
}
} // namespace

ServerMixer::ServerMixer(SessionManager& sessions, Config config)
    : sessions_(sessions),
      config_(config),
      mixer_(audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs), config_.sampleRate),
      spatialPolicy_(config_.spatial) {}

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
    for (const auto& speakerId : sttActiveSpeakers_) {
        if (stt_ && stt_->available()) stt_->endUtterance(speakerId);
    }
    sttActiveSpeakers_.clear();
    stt_ = stt;
    if (!stt_) return;
    stt_->setResultSink([this](const pipeline::SttResult& result) {
        protocol::SttTextMessage message;
        message.speakerId = result.speakerId;
        message.isFinal = result.isFinal;
        message.text = result.text;
        auto name = speakerNames_.find(result.speakerId);
        if (name != speakerNames_.end()) message.speakerName = name->second;
        message.utteranceId = result.utteranceId;

        // 每条识别结果都落日志（含说话人名字与文本原文）。这条日志是字幕链路的唯一事实来源：
        // 少了它，「字幕没出现」「字幕没有玩家名字」这两类问题在服务端侧完全无法定位。
        if (log_) {
            std::string const display = message.speakerName.empty() ? std::string("<empty>") : message.speakerName;
            log_(message.speakerName.empty(), "[stt] recognized name=\"" + display + "\""
                + " kind=" + (message.isFinal ? "final" : "partial")
                + " utterance=" + std::to_string(message.utteranceId)
                + " chars=" + std::to_string(message.text.size())
                + " text=\"" + message.text + "\"");
        }

        if (!message.text.empty() || message.isFinal) enqueueSttToAudible(message, steadyNowMs());
    });
}

bool ServerMixer::startFilePlayback(const std::filesystem::path& path, std::string& error) {
    return filePlayback_.load(path, config_.sampleRate, error);
}

void ServerMixer::stopFilePlayback() { filePlayback_.stop(); }

bool ServerMixer::filePlaybackActive() const { return filePlayback_.active(); }

std::string ServerMixer::filePlaybackName() const { return filePlayback_.fileName(); }

void ServerMixer::setSpeakerName(const protocol::PlayerId& speakerId, std::string speakerName) {
    // 空名不覆盖已有名字：入服早期取不到名字是常态，握手时会再解析一次补上。
    if (!speakerName.empty()) speakerNames_[speakerId] = std::move(speakerName);
}

void ServerMixer::beginSttUtterance(const protocol::PlayerId& speakerId, std::string speakerName) {
    if (!speakerName.empty()) speakerNames_[speakerId] = std::move(speakerName);
    if (!stt_ || !stt_->available()) return;
    stt_->beginUtterance(speakerId);
    sttActiveSpeakers_.insert(speakerId);
}

void ServerMixer::endSttUtterance(const protocol::PlayerId& speakerId) {
    if (!stt_ || !stt_->available()) return;
    // End 被控制线程接收时不立即调用：tickOnce 会先排空该玩家的抖动缓冲，
    // 下一次 tick 后再收尾，避免松键后的尾帧丢失。
    sttEndPending_.insert(speakerId);
}

void ServerMixer::tickOnce(int64_t nowMs) {
    // 节拍对齐：本函数由 ServerRuntime 在 ServerLevelTickEvent（50ms）里驱动，而混音周期是
    // config_.tickMs（默认 120ms）。若每个服务器 tick 都跑，一次会产出 framesPerTick(2) 帧
    // 60ms 音频，下行被放大到 ~2.4 倍实时速率，客户端抖动缓冲与渲染队列持续溢出丢帧
    // （实测 96 秒文件回放收到 3404 帧 ≈ 2.13 倍）。按 tickMs 限流后下行才是实时速率。
    if (hasMixedOnce_ && nowMs - lastTickMs_ < config_.tickMs) return;
    hasMixedOnce_ = true;
    lastTickMs_ = nowMs;
    ++tickCount_;

    const int frameSamples = audio::samplesPerFrame(config_.sampleRate, config_.frameSizeMs);
    const int framesPerTick = std::max(1, config_.tickMs / config_.frameSizeMs);

    auto sessions = sessions_.snapshot();

    // 收帧：PTT 边界由 ServerRuntime 通过 begin/end 驱动；这里仅负责把抖动缓冲
    // 中已经排空的 PCM 送入对应玩家的独立 STT 上下文。旧协议的音频 flags 仍兼容。
    for (const auto& session : sessions) {
        while (auto frame = session->pollFrame(nowMs)) {
            if (stt_ && stt_->available()) {
                if (frame->flags & protocol::AudioFlagStart) {
                    stt_->beginUtterance(session->id());
                    sttActiveSpeakers_.insert(session->id());
                }
                if (!frame->pcm.empty()) stt_->feedAudio(session->id(), frame->pcm);
                if (frame->flags & protocol::AudioFlagEnd) {
                    stt_->endUtterance(session->id());
                    sttActiveSpeakers_.erase(session->id());
                }
            }
            if (frame->pcm.empty()) continue;
            auto& queue = uploadQueues_[session->id()];
            if (queue.size() >= static_cast<std::size_t>(framesPerTick)) queue.pop_front();
            queue.push_back(std::move(frame->pcm));
        }
    }
    // 当前 tick 已经把每个会话的抖动缓冲排空；现在才提交 PTT 松开的 End，
    // 因而最后到达的音频帧已经进入 STT 队列。
    for (auto it = sttEndPending_.begin(); it != sttEndPending_.end();) {
        if (sttActiveSpeakers_.find(*it) != sttActiveSpeakers_.end()) {
            stt_->endUtterance(*it);
            sttActiveSpeakers_.erase(*it);
        }
        it = sttEndPending_.erase(it);
    }

    // 清理已离线说话者的积压（其中是原始 PCM，不能长期滞留）
    for (auto it = uploadQueues_.begin(); it != uploadQueues_.end();) {
        const bool online = std::any_of(sessions.begin(), sessions.end(), [&](auto const& session) {
            return session->id() == it->first;
        });
        if (online) ++it;
        else it = uploadQueues_.erase(it);
    }
    // 会话级状态同样要跟着离线清理，否则长期开服会按「来过的玩家数」持续增长。
    auto offline = [&sessions](const protocol::PlayerId& id) {
        return std::none_of(sessions.begin(), sessions.end(), [&](auto const& session) {
            return session->id() == id;
        });
    };
    for (auto it = balancers_.begin(); it != balancers_.end();) {
        if (offline(it->first)) it = balancers_.erase(it);
        else ++it;
    }
    for (auto it = encoders_.begin(); it != encoders_.end();) {
        if (offline(it->first)) it = encoders_.erase(it);
        else ++it;
    }
    for (auto it = mixSeqs_.begin(); it != mixSeqs_.end();) {
        if (offline(it->first)) it = mixSeqs_.erase(it);
        else ++it;
    }

    std::vector<float> mixed(static_cast<std::size_t>(frameSamples));
    for (int sub = 0; sub < framesPerTick; ++sub) {
        // 每个子帧重建混音输入：不同子帧用不同音频，避免同一段被编码两次
        mixer_.clearFrames();
        frameRms_.clear();
        for (auto& [speakerId, queue] : uploadQueues_) {
            if (queue.empty()) continue;
            auto pcm = std::move(queue.front());
            queue.pop_front();
            frameRms_[speakerId] = frameRms(pcm);
            mixer_.addSpeakerFrame(speakerId, std::move(pcm));
        }
        // 文件声源同样每个子帧取一帧，保证按实时速率播放
        const bool fileFrameFed = feedFilePlaybackFrame(frameSamples);
        if (!mixer_.hasActiveTalker()) continue;

        // 逐接收者重建增益矩阵：自我抑制 + 会话级空间策略 + 听者音量平衡 + 文件声源广播
        std::vector<protocol::PlayerId> audibleReceivers;
        audibleReceivers.reserve(sessions.size());
        for (const auto& receiver : sessions) {
            mixer_.clearGain(receiver->id());
            const auto listener = receiver->spatialState();
            const bool useProximity = proximityFor(*receiver);
            const auto balanceSettings = receiver->volumeBalance();
            const bool useBalance = useProximity && balanceSettings.enabled;
            auto& balancer = balancers_.try_emplace(receiver->id(), config_.frameSizeMs).first->second;
            size_t selected = 0;
            for (const auto& speaker : sessions) {
                if (speaker->id() == receiver->id()) continue; // 自我抑制
                // 只有本子帧真的送来音频的说话者才占用增益名额；否则静音会话会白占
                // maxTalkers 名额，也会让「本路是否有声」的判断失真。
                const auto rmsIt = frameRms_.find(speaker->id());
                if (rmsIt == frameRms_.end()) {
                    balancer.forget(speaker->id());
                    continue;
                }
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
                const float distanceGain = spatialPolicy_.gain(
                    query, useProximity ? audio::MixMode::Proximity : audio::MixMode::Global);
                if (distanceGain <= 0.0f) {
                    balancer.forget(speaker->id());
                    continue;
                }
                if (config_.maxTalkers != 0 && selected >= config_.maxTalkers) continue;
                // 先过音量平衡、再过近距离衰减：两级都是线性增益，乘在一起与
                // 「先处理 PCM 再按距离混合」等价，且不必额外拷贝一帧音频。
                const float balanceGain = useBalance
                    ? balancer.gainFor(speaker->id(), rmsIt->second, balanceSettings)
                    : 1.0f;
                mixer_.setGain(receiver->id(), speaker->id(), distanceGain * balanceGain);
                ++selected;
            }
            if (fileFrameFed) mixer_.setGain(receiver->id(), kFilePlaybackSourceId, 1.0f);
            // 本路没有任何可听来源时不编码、不发送：听不见就不该占用下行带宽。
            if (selected > 0 || fileFrameFed) audibleReceivers.push_back(receiver->id());
        }

        for (const auto& receiver : sessions) {
            if (std::find(audibleReceivers.begin(), audibleReceivers.end(), receiver->id()) == audibleReceivers.end()) {
                continue;
            }
            auto& encoder = encoders_[receiver->id()];
            if (!encoder) encoder = std::make_unique<codec::OpusEncoder>(config_.sampleRate, config_.channels, frameSamples, config_.bitrateKbps, config_.complexity, config_.enableDtx);
            auto& sequence = mixSeqs_[receiver->id()];
            std::vector<uint8_t> packet(static_cast<size_t>(encoder->maxPacketSize()));
            mixer_.computeMix(receiver->id(), mixed.data());
            const int encoded = encoder->encode(mixed.data(), packet.data(), packet.size());
            if (encoded <= 0) continue;
            protocol::MixStreamMessage message;
            message.seq = ++sequence;
            message.opusData.assign(packet.begin(), packet.begin() + encoded);
            enqueueTo(receiver->id(), message);
        }
    }
    // 输入帧只属于本 tick；清除后没有新上行帧时不会重复发送旧语音。
    mixer_.clearFrames();
}

bool ServerMixer::feedFilePlaybackFrame(int frameSamples) {
    if (!filePlayback_.active()) return false;
    auto frame = filePlayback_.nextFrame(static_cast<std::size_t>(frameSamples));
    if (frame.empty()) return false;
    mixer_.addSpeakerFrame(kFilePlaybackSourceId, std::move(frame));
    return true;
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

void ServerMixer::enqueueSttToAudible(const protocol::SttTextMessage& message, int64_t nowMs) {
    const auto sessions = sessions_.snapshot();
    for (const auto& receiver : sessions) {
        // 说话者本人始终能看到自己的字幕：语音链路对他自己被自我抑制，但「我说了什么」
        // 不应当跟着消失；字幕的意义是让听者看见听得见的话，而不是隐藏自己的话。
        if (receiver->id() == message.speakerId) {
            enqueueTo(receiver->id(), message);
            continue;
        }
        auto speaker = sessions_.find(message.speakerId);
        if (!speaker) continue; // 说话者已离线：不投递残留字幕
        if (!audibleFor(*receiver, *speaker, nowMs)) continue;
        enqueueTo(receiver->id(), message);
    }
}

bool ServerMixer::audibleFor(const PlayerSession& receiver, const PlayerSession& speaker, int64_t nowMs) const {
    const auto listener = receiver.spatialState();
    const auto source = speaker.spatialState();
    const bool fresh = source.positionKnown && nowMs >= source.updatedAtMs &&
        nowMs - source.updatedAtMs <= config_.staleMs;
    audio::SpatialGainQuery query;
    query.listener = receiver.id();
    query.speaker = speaker.id();
    query.sameDimension = listener.dimensionId == source.dimensionId;
    query.positionKnown = fresh && listener.positionKnown;
    if (query.positionKnown) {
        const float dx = listener.x - source.x;
        const float dy = listener.y - source.y;
        const float dz = listener.z - source.z;
        query.distBlocks = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return spatialPolicy_.gain(
        query, proximityFor(receiver) ? audio::MixMode::Proximity : audio::MixMode::Global) > 0.0f;
}

bool ServerMixer::proximityFor(const PlayerSession& session) const {
    return config_.spatial.mode == audio::MixMode::Proximity && session.proximityEnabled();
}

void ServerMixer::enqueueTo(const protocol::PlayerId& peerId, const protocol::Message& message) {
    std::lock_guard lock(pendingMutex_);
    if (config_.maxPending == 0) return;
    if (pending_.size() >= config_.maxPending) pending_.pop_front();
    pending_.emplace_back(peerId, message);
}

} // namespace bsc::server
