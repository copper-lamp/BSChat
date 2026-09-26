#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/protocol/Message.h"

namespace bsc::pipeline {

// 转写结果（字幕数据）
struct SttResult {
    protocol::PlayerId speakerId{};
    bool isFinal = false; // false=部分结果（增量），true=最终结果
    std::string text;
};

// 语音转文字抽象（流式语义）。
// v1 实现为服务端本地 sherpa-onnx（流式 Zipformer），结果通过回调异步返回。
//
// 话语生命周期：beginUtterance（PTT Start 帧）→ feedAudio（逐帧喂入）
// → endUtterance（PTT End 帧）。feedAudio 期间周期产出部分结果
// （isFinal=false），endUtterance 产出最终结果（isFinal=true）。
class IStt {
public:
    virtual ~IStt() = default;

    // 结果回调（worker 线程触发）：isFinal=false 为增量部分结果，true 为最终结果
    using ResultSink = std::function<void(const SttResult&)>;

    // 话语开始：为 speakerId 开启识别上下文（重复 begin 视为重启旧句）
    virtual void beginUtterance(const protocol::PlayerId& speakerId) = 0;

    // 话语进行中喂入解码 PCM（float [-1,1]，48kHz mono）
    virtual void feedAudio(const protocol::PlayerId& speakerId, const std::vector<float>& pcm) = 0;

    // 话语结束：产出最终结果并释放上下文（未 begin 则忽略）
    virtual void endUtterance(const protocol::PlayerId& speakerId) = 0;

    // 装配结果回调（装配期调用，之后不再变更）
    virtual void setResultSink(ResultSink sink) = 0;

    // 引擎是否就绪（模型加载成功）。false → 所有输入被丢弃（降级）
    virtual bool available() const = 0;

    // 停止并释放资源（模型/线程）
    virtual void shutdown() = 0;
};

} // namespace bsc::pipeline
