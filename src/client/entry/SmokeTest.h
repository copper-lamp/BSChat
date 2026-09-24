#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "client/entry/ClientRuntime.h"

namespace vc::client {

// 进入服务器后自动执行的双端链路自检。
//
// 目标：在没有第二个真实客户端的前提下，用真实合成的语音音调走通
//   客户端采集缓冲 -> AGC -> Opus 编码 -> 协议上行 -> 服务端解码/混音
//   -> MixStream 回传 -> 客户端抖动缓冲 -> Opus 解码 -> 渲染
// 完整链路，并把每一步的成功/失败写进客户端日志，便于人工核对。
//
// 线程模型：全部状态只在客户端主线程（tick）推进，音频音调由 tick 定时喂入，
// 不引入额外线程；WASAPI 采集线程不参与本自检。
class SmokeTest final {
public:
    enum class Stage {
        Idle,
        WaitingForReady,   // 已进服，等待 Welcome 握手完成
        Uploading,         // 正在持续上行合成音调
        AwaitingDownlink,  // 上行结束，等待服务端回传混音
        Done,
    };

    explicit SmokeTest(ClientRuntime& runtime);

    // 诊断日志出口。本类不直接依赖 LeviLamina，日志由外层宿主注入，
    // 未注入时全部诊断静默丢弃（与 ServerRuntime::LogSink 同一约定）。
    using LogSink = std::function<void(bool isError, std::string const& message)>;
    void setLogSink(LogSink sink) { logSink_ = std::move(sink); }

    // 进服后调用，开始一次自检。
    void begin();

    // 由 ClientLevelTickEvent 驱动推进；nowMs 为单调毫秒时钟。
    void tick(int64_t nowMs);

    void reset();
    bool finished() const { return stage_ == Stage::Done; }

private:
    void log(const std::string& text) const;
    void report(const std::string& name, bool ok, const std::string& detail) const;
    void finish(int64_t nowMs);

    // 生成一帧语音音调 PCM（正弦叠加，幅度在 audible 范围内）。
    void fillTone(int64_t nowMs);

    ClientRuntime& runtime_;
    LogSink logSink_;
    Stage stage_ = Stage::Idle;
    int64_t beginMs_ = 0;
    int64_t nextToneMs_ = 0;
    int64_t uploadStopMs_ = 0;
    int64_t deadlineMs_ = 0;
    uint32_t uplinkFrames_ = 0;
    double tonePhase_ = 0.0;
    std::vector<float> toneBuffer_;
    bool reportedHandshake_ = false;
};

} // namespace vc::client
