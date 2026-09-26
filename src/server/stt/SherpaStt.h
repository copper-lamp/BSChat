#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/pipeline/IStt.h"

namespace bsc::server {

// 服务端本地流式语音转文字：基于 sherpa-onnx（流式 Zipformer）。
// 零 LeviLamina 依赖，可纯 host 单测。
//
// 流式管线（worker 线程）：
//   beginUtterance → 开启/重置识别上下文；
//   feedAudio      → 48k→16k 降采样后累积，累积达到 partialIntervalMs 产出部分结果
//                    （isFinal=false，供字幕增量显示）；
//   endUtterance   → 对整段产出最终结果（isFinal=true）并释放上下文。
//   maxUtteranceMs 超长 → 自动切分：先产出最终结果再开新上下文，不丢句尾。
//
// 引擎集成：BSC_ENABLE_SHERPA 开启时接入 sherpa-onnx（vendoring 后实现
// transcribePartial/transcribeFinal）；未开启或模型缺失 → available()=false，
// 所有输入被丢弃，语音主链路不受影响（降级）。
class SherpaStt : public pipeline::IStt {
public:
    struct Options {
        std::string libraryPath;
        std::string encoderPath;       // 流式 Zipformer encoder.onnx
        std::string decoderPath;       // decoder.onnx
        std::string joinerPath;        // joiner.onnx
        std::string tokensPath;        // tokens.txt
        int threads = 4;               // 推理线程数
        size_t maxQueued = 64;         // 喂帧队列上限：超限丢最旧
        int partialIntervalMs = 600;   // 部分结果产出间隔（累积音频时长，ms）
        size_t maxUtteranceMs = 30000; // 单句上限，超长强制切分
    };

    // 日志回调（可在任意线程触发），可空
    using LogFn = std::function<void(const std::string&)>;
    using ResultSink = pipeline::IStt::ResultSink;

    explicit SherpaStt(Options options, LogFn log = {});
    ~SherpaStt() override;
    SherpaStt(const SherpaStt&) = delete;
    SherpaStt& operator=(const SherpaStt&) = delete;

    // IStt
    void beginUtterance(const protocol::PlayerId& speakerId) override;
    void feedAudio(const protocol::PlayerId& speakerId, const std::vector<float>& pcm) override;
    void endUtterance(const protocol::PlayerId& speakerId) override;
    void setResultSink(ResultSink sink) override;
    bool available() const override { return available_.load(); }
    void shutdown() override;

    // 48kHz → 16kHz：3:1 整数降采样 + 3 点移动平均抗混叠（sherpa-onnx 输入规格）
    static std::vector<float> resample48kTo16k(const std::vector<float>& pcm48k);

protected:
    // 引擎抽象（sherpa-onnx vendoring 后实现；子类可覆盖注入假引擎用于单测）。
    // 输入 16kHz mono PCM（float [-1,1]），返回转写文本；空串表示失败。
    // 引擎就绪后由构造器置 available_ = true。
    virtual std::string transcribePartial(const std::vector<float>& pcm16k);
    virtual std::string transcribeFinal(const std::vector<float>& pcm16k);

    std::atomic<bool> available_{false};

private:
    struct Op {
        enum class Kind : uint8_t { Begin, Feed, End };
        Kind kind = Kind::Begin;
        protocol::PlayerId speakerId{};
        std::vector<float> pcm; // Feed 时携带（48k mono）
    };

    struct SpeakerCtx {
        std::vector<float> pcm16k;
        size_t lastPartialMs = 0;
        size_t fedSamples = 0;
        void* stream = nullptr;
    };

    void workerMain();
    void enqueue(Op op);
    void emitResult(const protocol::PlayerId& speakerId, bool isFinal, const std::string& text);

    Options options_;
    LogFn log_;
    ResultSink sink_;
    std::mutex sinkMutex_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    void* library_ = nullptr;
    void* recognizer_ = nullptr;
    void* activeStream_ = nullptr;
    size_t activeFedSamples_ = 0;
    void* api_ = nullptr;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Op> queue_;

    // 仅 worker 线程访问
    std::map<protocol::PlayerId, SpeakerCtx, std::less<>> contexts_;
};

} // namespace bsc::server
