#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/protocol/Message.h"

namespace bsc::server {

// 文件声源的虚拟标识：真实玩家 UUID 不可能是全 0xFF，故不会与玩家冲突。
// 它不是任何接收者本人，因此天然不受“不把自己的声音混给自己”的自我抑制影响。
inline constexpr protocol::PlayerId kFilePlaybackSourceId = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// 混音器的文件声源：把一段 WAV 按帧长逐帧喂给 ServerMixer，用于用真实音频
// （音乐/录音）验证「编码 → 传输 → 抖动缓冲 → 解码 → 设备播放」的听感。
// 线程模型：load/stop 由命令线程调用，nextFrame 由混音 tick 调用，内部加锁。
class FilePlaybackSource {
public:
    // 加载 WAV 并下混重采样到 targetSampleRate 单声道；成功即进入播放状态。
    bool load(const std::filesystem::path& path, int targetSampleRate, std::string& error);
    void stop();
    bool active() const;

    // 取下一帧：frameSamples 为单帧采样数；剩余不足时用静音补齐并结束。
    // 已无数据返回空 vector。
    std::vector<float> nextFrame(std::size_t frameSamples);

    std::size_t remainingSamples() const;
    int sampleRate() const;
    std::string fileName() const;

private:
    mutable std::mutex mutex_;
    std::vector<float> pcm_;
    std::size_t cursor_ = 0;
    int sampleRate_ = 0;
    std::string fileName_;
};

} // namespace bsc::server
