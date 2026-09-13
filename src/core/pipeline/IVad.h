#pragma once

#include <cstddef>

namespace vc::pipeline {

// 语音活动检测抽象。
// v1 默认关闭（PTT 触发），后续接入采集触发源时替换实现即可。
class IVad {
public:
    virtual ~IVad() = default;

    // 输入一帧 PCM（float [-1,1]），返回该帧是否有人声
    virtual bool process(const float* pcm, size_t sampleCount) = 0;

    virtual void reset() = 0;
};

} // namespace vc::pipeline
