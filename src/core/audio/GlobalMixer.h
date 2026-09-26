#pragma once

#include <map>
#include <vector>

#include "core/pipeline/IMixer.h"
#include "core/protocol/Message.h"

namespace bsc::audio {

// 全局混音器：所有活跃说话者等权重叠加，输出一路混合帧。
// 归一化策略：说话者增益 = 1/sqrt(活跃数)（功率归一），输出限幅 [-1,1]。
// 该实现位于 core 以便纯 host 单测；服务端适配层只负责喂帧与取帧。
class GlobalMixer : public pipeline::IMixer {
public:
    struct Config {
        int sampleRate = 48000;
        int frameSamples = 2880; // 混音粒度（60ms 帧；服务端每 tick 调用多次凑满下行 120ms）
    };

    explicit GlobalMixer(Config config);

    void addFrame(const protocol::PlayerId& speaker, std::vector<float> pcm) override;
    bool hasActiveTalker() const override;
    void mix(float* out) override;
    void removeSpeaker(const protocol::PlayerId& speaker) override;

    int frameSamples() const override { return config_.frameSamples; }
    int sampleRate() const override { return config_.sampleRate; }

private:
    struct SpeakerBuffer {
        std::vector<float> data; // 待混音的 PCM
    };

    Config config_;
    std::map<protocol::PlayerId, SpeakerBuffer> speakers_;
};

} // namespace bsc::audio
