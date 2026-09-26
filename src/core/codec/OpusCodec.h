#pragma once

#include <cstddef>
#include <cstdint>

// Opus 编解码封装。
// 线程安全约定：每个编码器/解码器实例只能被一个线程使用；
// 服务端每发送者一个解码器、混音输出一个编码器，客户端采集/播放各一个，天然满足。

namespace bsc::codec {

class OpusEncoder {
public:
    // sampleRate: 48000; channels: 1; frameSamples: 采样率*帧长/1000
    // bitrateKbps: 目标码率；complexity: 0-10；dtx: 静音时是否只发超低码率包（省带宽，
    // 但会让安静段落变成"无数据"，对连续音频/音乐听感不利）
    OpusEncoder(int sampleRate, int channels, int frameSamples, int bitrateKbps, int complexity = 10, bool dtx = true);
    ~OpusEncoder();
    OpusEncoder(const OpusEncoder&) = delete;
    OpusEncoder& operator=(const OpusEncoder&) = delete;

    // 编码一帧 PCM(float, [-1,1])。
    // 返回编码字节数；返回 0 表示该帧被判定为静音（DTX），未产生有效数据。
    // out 容量必须 >= maxPacketSize()。
    int encode(const float* pcm, uint8_t* out, size_t outCapacity);

    // 重置内部状态（丢包/切换会话时调用）
    void reset();

    int maxPacketSize() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

class OpusDecoder {
public:
    OpusDecoder(int sampleRate, int channels, int frameSamples);
    ~OpusDecoder();
    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    // 解码一帧 Opus 数据到 pcm(float)。
    // data 为空或 size==0 时执行丢包隐藏（PLC），输出一帧推测值。
    // 返回解码样本数；失败返回负值。
    int decode(const uint8_t* data, size_t size, float* pcm, size_t capacity);

    void reset();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace bsc::codec
