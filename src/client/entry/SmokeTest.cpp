#include "client/entry/SmokeTest.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vc::client {

namespace {

// 自检时间轴（毫秒，相对于开始时刻）
constexpr int64_t kReadyWaitMs = 5000;    // 等待握手完成
constexpr int64_t kUploadDurationMs = 1500; // 持续上行时长
constexpr int64_t kDownlinkWaitMs = 3000; // 上行结束后的回传等待
constexpr double kToneHz = 440.0;         // 语音频段内的测试音
constexpr float kToneAmplitude = 0.25F;   // 留足余量，避免 AGC/限幅削顶

} // namespace

SmokeTest::SmokeTest(ClientRuntime& runtime) : runtime_(runtime) {}

void SmokeTest::log(const std::string& text) const {
    if (logSink_) logSink_(false, "[smoke] " + text);
}

void SmokeTest::report(const std::string& name, bool ok, const std::string& detail) const {
    if (logSink_) {
        logSink_(false, "[smoke] " + name + " = " + (ok ? "PASS" : "FAIL") + " " + detail);
    }
}

void SmokeTest::begin() {
    stage_ = Stage::WaitingForReady;
    beginMs_ = 0;
    nextToneMs_ = 0;
    uploadStopMs_ = 0;
    deadlineMs_ = 0;
    uplinkFrames_ = 0;
    tonePhase_ = 0.0;
    reportedHandshake_ = false;
    // 按帧长节拍喂入，与真实采集一致：每 frameSizeMs 产出一帧 frameSizeMs 的音频。
    // 若快于实时（早期固定 20ms 喂 60ms 帧），服务端会话限速会按比例丢弃并刷告警。
    toneIntervalMs_ = std::max<int64_t>(1, runtime_.config().audio.frameSizeMs);
    log("smoke test requested by the server /voicechat test command; it runs as soon as the handshake completes");
}

void SmokeTest::reset() {
    stage_ = Stage::Idle;
    uplinkFrames_ = 0;
}

void SmokeTest::fillTone(int64_t nowMs) {
    const int sampleRate = std::max(8000, runtime_.config().audio.sampleRate);
    const int frameSamples = std::max(1, sampleRate * std::max(1, runtime_.config().audio.frameSizeMs) / 1000);
    toneBuffer_.resize(static_cast<std::size_t>(frameSamples));

    const double step = 2.0 * 3.14159265358979323846 * kToneHz / static_cast<double>(sampleRate);
    for (int i = 0; i < frameSamples; ++i) {
        toneBuffer_[static_cast<std::size_t>(i)] = kToneAmplitude * static_cast<float>(std::sin(tonePhase_));
        tonePhase_ += step;
        if (tonePhase_ > 2.0 * 3.14159265358979323846) tonePhase_ -= 2.0 * 3.14159265358979323846;
    }
    (void)nowMs;

    // 走真实上行路径：AGC -> Opus 编码 -> 协议发送。
    runtime_.submitPcm(toneBuffer_.data(), toneBuffer_.size());
    ++uplinkFrames_;
}

void SmokeTest::tick(int64_t nowMs) {
    if (stage_ == Stage::Idle || stage_ == Stage::Done) return;
    if (beginMs_ == 0) beginMs_ = nowMs;

    const int64_t elapsed = nowMs - beginMs_;

    if (stage_ == Stage::WaitingForReady) {
        if (runtime_.state() == ClientRuntime::State::Ready) {
            report("handshake", true, "(client reached Ready)");
            reportedHandshake_ = true;
            stage_ = Stage::Uploading;
            uploadStopMs_ = nowMs + kUploadDurationMs;
            nextToneMs_ = nowMs;
            // The uplink path only transmits while push-to-talk is held, which is
            // exactly the gate a real key press drives.
            runtime_.setTalking(true);
            log("handshake complete; starting synthesized-voice uplink");
        } else if (runtime_.state() == ClientRuntime::State::Failed) {
            report("handshake", false, "(client entered Failed state)");
            stage_ = Stage::Done;
        } else if (elapsed > kReadyWaitMs) {
            report("handshake", false, "(timeout waiting for Welcome)");
            stage_ = Stage::Done;
        }
        return;
    }

    if (stage_ == Stage::Uploading) {
        if (uploadStopMs_ == 0) uploadStopMs_ = nowMs + kUploadDurationMs;
        while (nowMs >= nextToneMs_ && nextToneMs_ < uploadStopMs_) {
            fillTone(nowMs);
            nextToneMs_ += toneIntervalMs_;
        }
        if (nowMs >= uploadStopMs_) {
            runtime_.setTalking(false);
            const uint64_t sent = runtime_.sentAudioFrames();
            report("uplink", sent > 0, "(encoded+framed voice packets=" + std::to_string(sent) + ")");
            stage_ = Stage::AwaitingDownlink;
            deadlineMs_ = nowMs + kDownlinkWaitMs;
            log("uplink finished; waiting for server MixStream");
        }
        return;
    }

    if (stage_ == Stage::AwaitingDownlink) {
        const uint64_t received = runtime_.receivedMixFrames();
        const uint64_t played = runtime_.playedMixFrames();
        if (received > 0 && played > 0) {
            report("downlink", true, "(MixStream received=" + std::to_string(received)
                + ", decoded+played=" + std::to_string(played) + ")");
            finish(nowMs);
        } else if (nowMs >= deadlineMs_) {
            report("downlink", false, "(MixStream received=" + std::to_string(received)
                + ", decoded+played=" + std::to_string(played) + ")");
            finish(nowMs);
        }
    }
}

void SmokeTest::finish(int64_t nowMs) {
    (void)nowMs;
    stage_ = Stage::Done;
    const bool ok = runtime_.sentAudioFrames() > 0 && runtime_.receivedMixFrames() > 0
        && runtime_.playedMixFrames() > 0 && reportedHandshake_;
    report("overall", ok, ok ? "(handshake + uplink + downlink + local decode verified)"
                             : "(see the individual results above)");
    log("smoke test finished; send this log to the developer");
}

} // namespace vc::client
