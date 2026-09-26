#pragma once

#include <string_view>

namespace vc::client::hud {

// 状态覆盖层要表达的状态。图标与文案分别由绘制层与 i18n 键决定。
enum class AudioStatus {
    Idle,     // 未进入语音会话
    Speaking, // 本端正在说话
    Muted,    // 本端已静音
    Playing,  // 正在收听他人
    Silent,   // 会话中，既未说也未听
};

// 状态来源：由入口层把配置、握手状态与音频状态整理成一组输入。
struct StatusInputs {
    bool inSession = false; // 握手完成、语音会话可用
    bool talking = false;   // 本端正在上行
    bool muted = false;     // 本端静音
    bool playing = false;   // 正在放音
};

// 纯状态推导：优先级 静音 > 说话 > 放音 > 会话中无声；未进会话一律空闲。
class StatusOverlay final {
public:
    void update(StatusInputs inputs) { inputs_ = inputs; }
    AudioStatus status() const;

    static std::string_view i18nKey(AudioStatus status);

private:
    StatusInputs inputs_{};
};

} // namespace vc::client::hud
