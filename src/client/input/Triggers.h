#pragma once

#include <cstdint>
#include <functional>

namespace bsc::client::input {

struct InputEvent { bool pressed = false; uint32_t key = 0; int64_t atMs = 0; };
using TriggerCallback = std::function<void(bool active, int64_t atMs)>;

class TalkTrigger {
public:
    explicit TalkTrigger(TriggerCallback callback = {});
    void onInput(const InputEvent& event);
    bool active() const { return active_; }
private: TriggerCallback callback_; bool active_ = false;
};

class PttTrigger final : public TalkTrigger {
public: explicit PttTrigger(uint32_t key, TriggerCallback callback = {});
    void onKey(uint32_t key, bool pressed, int64_t atMs);
private: uint32_t key_;
};

// 能量式自动语音检测（VAD）：按采集帧的 RMS 判定“正在说话”，驱动上行语音开关。
//
// 为什么不能只用固定门限：麦克风增益与环境噪声在玩家之间差一个数量级，固定门限要么
// 永远触发不了（安静环境），要么在噪声里一直触发（嘈杂环境）。因此这里以「自适应噪声底」
// 为主门限，构造参数里的 threshold 只作为绝对下限，保证噪声底再高也不会全时误触发。
//
// 迟滞：起始门限高于停止门限（startRatio > stopRatio），避免在门限附近来回抖动导致
// 上行语音被频繁开关（每次开关都会重启一句 STT 上下文，字幕会碎成很多条）。
//
// 起停都需要持续时间：onsetMs 过滤敲键盘/碰麦的瞬时尖峰，holdMs 保住句尾的气口。
class VadTrigger final : public TalkTrigger {
public:
    struct Options {
        float floor = 0.0F;      // 初始噪声底估计（0 表示从第一帧开始学）
        float startRatio = 3.5F; // 起始门限 = max(threshold, 噪声底 * startRatio)
        float stopRatio = 1.8F;  // 停止门限 = max(threshold * stopFactor, 噪声底 * stopRatio)
        float stopFactor = 0.6F; // 停止门限相对绝对下限的折扣
        float floorUp = 0.004F;  // 噪声底上升系数（慢）：讲话不能抬高噪声底
        float floorDown = 0.20F; // 噪声底下降系数（快）：环境变安静要快速跟上
        int onsetMs = 60;        // 连续超起始门限多久才真正开始说话
        int holdMs = 350;        // 连续低于停止门限多久才真正结束说话
    };

    // threshold：绝对起始门限下限；holdMs：兼容旧调用，表示 holdMs（onsetMs 取 0）。
    explicit VadTrigger(float threshold = 0.02F, int holdMs = 180, TriggerCallback callback = {});
    VadTrigger(float threshold, int holdMs, int onsetMs, TriggerCallback callback);

    void onLevel(float rms, int64_t atMs);
    // 会话切换（进/出世界）时调用：结束当前话语并清空迟滞计时，噪声底保留（环境没变）。
    void reset(int64_t atMs);

    float noiseFloor() const { return noiseFloor_; }
    float startThreshold() const;
    float stopThreshold() const;

private:
    float threshold_; Options options_;
    float noiseFloor_ = 0.0F;
    int64_t aboveSinceMs_ = -1;
    int64_t belowSinceMs_ = -1;
};

} // namespace bsc::client::input
