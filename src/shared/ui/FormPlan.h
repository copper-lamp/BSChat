#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "shared/ui/PanelDefinition.h"

namespace vc::ui {

// 面板值：布尔 → uint64_t(0/1)，数值 → double，文本 → std::string；
// monostate 表示「该键不存在或不可读」。双端共用：客户端映射 ClientConfig，服务端映射 ServerConfig。
using PanelValue = std::variant<std::monostate, uint64_t, double, std::string>;
using PanelValues = std::unordered_map<std::string, PanelValue>;

// 当前值读取器：返回某配置键的当前值，作为表单默认值。
using PanelValueReader = std::function<PanelValue(std::string const& key)>;
// 文案解析器：i18n 键 → 显示文本；未命中时由调用方回落键名。
using PanelTextResolver = std::function<std::string(std::string_view key)>;

// 单个控件的中性入参描述。与 ll::form 解耦，便于 host 单测「定义 → 控件入参」的映射。
struct FormControlPlan {
    PanelElementType type = PanelElementType::Label;
    std::string name;                // 交互控件绑定键（同时作为表单元素名）
    std::string text;                // 已解析文案
    std::string placeholder;
    std::vector<std::string> options; // 已解析选项文案
    std::size_t defaultIndex = 0;
    bool defaultBool = false;
    double min = 0.0;
    double max = 0.0;
    double step = 1.0;
    double defaultNumber = 0.0;
    std::string defaultString;
};

// 把面板定义 + 当前值映射为控件计划，顺序与定义一致。
// step <= 0 的滑块会收敛到 1.0：LeviLamina 的 Slider 要求 step > 0，否则整条控件不会进入表单内容。
std::vector<FormControlPlan> buildFormPlan(
    PanelDefinition const& definition,
    PanelValueReader const& readValue,
    PanelTextResolver const& resolveText
);

// 解析客户端提交的原始响应。CustomForm 的响应是「按定义顺序、每个元素一项」的 JSON 数组
// （LeviLamina CustomFormHandler 以 content 数组下标对齐元素），这里按定义里交互控件的顺序与
// 预期类型转成 PanelValues：toggle→bool、dropdown→选项下标、slider→数值、input→字符串。
// 解析失败返回空 map 并写 error。
PanelValues parseFormResponse(
    PanelDefinition const& definition,
    std::string_view payloadJson,
    std::string* error = nullptr
);

} // namespace vc::ui
