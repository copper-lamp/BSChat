#pragma once

#include <map>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/protocol/Message.h"

namespace bsc::audio {

// 逐接收者（per-receiver）权重混音引擎。与全局混音器不同，本引擎对每个接收者
// 维护独立的一路增益矩阵，可表达"空间选路"——不同位置的接收者听到不同说话者。
//
// 用法：
//   1. 喂入说话者本帧 PCM：addSpeakerFrame(speaker, pcm)（同一说话者重复喂入覆盖旧帧）。
//   2. 建立选路矩阵：setGain(listener, speaker, gain)（缺省增益=0，即听不到）。
//      clearGain(listener) 可在每 tick 重建矩阵前清空某个接收者的全部增益。
//   3. 输出某接收者听到的混合：computeMix(listener, out)。
//
// 自我抑制（听不到自己）不在此特判，由上层调用方在填增益矩阵时置 0 实现。
// 语义：己方语音不进自己的混音帧，仅作为其他接收者的 signal source。
class MixerCore {
public:
    MixerCore(int frameSamples, int sampleRate);

    // 喂入一个说话者本帧 PCM（float [-1,1]，建议长度 == frameSamples；重复喂同一
    // speaker 视为覆盖旧帧）。长度不足 frameSamples 的帧按实际长度参与求和。
    void addSpeakerFrame(protocol::PlayerId speaker, FloatVector pcm);

    // 设置/更新接收者对某说话者的增益（gain>=0；<0 按 0 处理）。
    void setGain(protocol::PlayerId listener, protocol::PlayerId speaker, float gain);

    // 清空指定接收者的全部增益（常用于每 tick 重建矩阵前）。
    void clearGain(protocol::PlayerId listener);

    // 把该接收者的各路加权求和归一化到 out（长度 frameSamples），并限幅 [-1,1]。
    // 归一化策略参照 GlobalMixer 的 1/sqrt(N) 功率归一，N 为该接收者实际参与的非零
    // 增益说话者数（既有帧又有非零增益），逐路再叠加各自 gain，最后限幅。
    void computeMix(protocol::PlayerId listener, float* out) const;

    // 是否有活跃说话者（存在帧数据）。
    bool hasActiveTalker() const;

    // 丢弃当前 tick 的说话者帧；增益矩阵保留供下一 tick 重建。
    void clearFrames();

    int frameSamples() const { return frameSamples_; }
    int sampleRate() const { return sampleRate_; }

    // 清空所有帧与全部增益矩阵（恢复到初始状态）。
    void reset();

private:
    int frameSamples_;
    int sampleRate_;
    // speaker -> 本帧 PCM（覆盖语义：每帧喂入替换内存）
    std::map<protocol::PlayerId, FloatVector> frames_;
    // listener -> (speaker -> gain)
    std::map<protocol::PlayerId, std::map<protocol::PlayerId, float>> gains_;
};

} // namespace bsc::audio