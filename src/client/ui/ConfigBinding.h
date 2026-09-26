#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

#include "core/config/Config.h"

namespace vc::client::ui {

// 面板定义里的配置键与 ClientConfig 字段之间的映射。
// 本模块零 LeviLamina 依赖（不含 ll::form 的类型），可 host 单测；
// 表单结果由 FormPanelBuilder 归一化成 PanelValue 后交给这里。

// 取值约定：布尔 → uint64_t(0/1)，数值 → double，文本 → std::string；
// monostate 表示「该键不存在或不可读」。
using PanelValue = std::variant<std::monostate, uint64_t, double, std::string>;
using PanelValues = std::unordered_map<std::string, PanelValue>;

class ConfigBinding final {
public:
    // 该键是否由本映射支持；面板定义校验与日志用。
    static bool supported(std::string const& key);

    // 读取当前配置值，作为表单默认值。未知键返回 monostate。
    static PanelValue read(config::ClientConfig const& config, std::string const& key);

    // 应用表单结果。未知键、类型不符、越界值一律忽略并保留原值。
    // 返回被接受并写入的键数量。
    static std::size_t apply(config::ClientConfig& config, PanelValues const& values);
};

// 支持的键（面板定义 JSON 里的 element.key）：
//   voiceEnabled        bool   语音总开关
//   talkMode            文本   disabled | pushToTalk | voiceActivity（派生自 voiceEnabled/vadEnabled）
//   captureEnabled      bool   采集开关（关闭后只收听）
//   playbackVolume      数值   播放音量 0..100（映射到 ClientConfig 的 0..1）
//   subtitleEnabled     bool   字幕开关
//   maxSubtitleLines    数值   字幕行数上限 1..8
//   hudEnabled          bool   状态覆盖层开关

} // namespace vc::client::ui
