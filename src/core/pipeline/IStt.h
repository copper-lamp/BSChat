#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/protocol/Message.h"

namespace vc::pipeline {

// 转写结果（字幕数据）
struct SttResult {
    protocol::PlayerId speakerId{};
    bool isFinal = false;
    std::string text;
};

// 语音转文字抽象。
// v1 实现为服务端本地 Whisper（whisper.cpp），结果异步回调。
class IStt {
public:
    virtual ~IStt() = default;

    // 提交一段话语进行转写；结果通过回调异步返回（可在任意线程触发）
    virtual void submit(
        const protocol::PlayerId& speakerId,
        std::vector<float> pcm,
        std::function<void(SttResult)> onResult
    ) = 0;

    // 停止并释放资源（模型/线程）
    virtual void shutdown() = 0;
};

} // namespace vc::pipeline
