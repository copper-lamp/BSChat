#pragma once

#include <cstdint>
#include <vector>

#include "core/protocol/Message.h"

namespace bsc::pipeline {

// 混音器抽象。
// v1 实现为全局混音（GlobalMixer，位于 core/audio）；
// 后续环境混音/位置混音通过新增实现替换，业务侧零改动。
class IMixer {
public:
    virtual ~IMixer() = default;

    // 送入一路说话者的 PCM（float [-1,1]，长度任意，内部按帧对齐缓冲）
    virtual void addFrame(const protocol::PlayerId& speaker, std::vector<float> pcm) = 0;

    // 是否有活跃说话者（决定是否推流）
    virtual bool hasActiveTalker() const = 0;

    // 产出一帧混音结果；out 容量必须 >= frameSamples()
    virtual void mix(float* out) = 0;

    // 移除说话者（会话离开时）
    virtual void removeSpeaker(const protocol::PlayerId& speaker) = 0;

    virtual int frameSamples() const = 0;
    virtual int sampleRate() const = 0;
};

} // namespace bsc::pipeline
