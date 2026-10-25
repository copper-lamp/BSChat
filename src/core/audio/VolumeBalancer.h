#pragma once

#include <cstddef>
#include <map>

#include "core/audio/AudioTypes.h"
#include "core/protocol/Message.h"

namespace bsc::audio {

// 收听端的音量平衡参数：服务端把每个说话者的响度向 targetRms 拉平。
// 参数是全局的（不区分说话者），每个听者一份，由客户端上报。
struct VolumeBalanceSettings {
    bool enabled = false;
    float strength = 1.0f;   // 0..1
    float targetRms = 0.12f; // 线性帧 RMS
};

// 服务端逐（听者, 说话者）响度平衡器。只输出线性增益：把它乘进混音权重与
// 「先把音频过一遍增益再加权求和」在数学上等价，且不额外拷贝 PCM。
//
// 线程模型：一个听者一个实例，只由音频线程调用，无需加锁。
class VolumeBalancer {
public:
    explicit VolumeBalancer(int frameSizeMs = kDefaultFrameSizeMs);

    // 输入说话者本帧 RMS，返回本帧应乘的增益。增益跨帧平滑：
    //  - 需要压小（说话太响）时快速跟随（attack ~30ms），避免突然的爆音；
    //  - 需要抬大（说话太轻）时缓慢释放（release ~400ms），避免把底噪/停顿放大。
    float gainFor(const protocol::PlayerId& speaker, float frameRms, const VolumeBalanceSettings& settings);

    // 丢弃某说话者的平滑状态（离线/长时间不说话）。
    void forget(const protocol::PlayerId& speaker);
    void reset();
    std::size_t trackedSpeakers() const { return states_.size(); }

    static constexpr float kMinGain = 0.2f;   // 最多压 ~14 dB
    static constexpr float kMaxGain = 5.0f;   // 最多抬 ~14 dB
    static constexpr float kSilenceRms = 1e-4f; // 低于此视为静音帧，不更新增益

private:
    struct State {
        float gain = 1.0f;
    };

    float smooth(float current, float desired) const;

    int frameSizeMs_;
    std::map<protocol::PlayerId, State> states_;
};

} // namespace bsc::audio
