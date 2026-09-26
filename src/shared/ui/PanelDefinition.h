#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vc::ui {

// 面板定义：把「面板长什么样」从 C++ 里拿出来，交给内置 JSON 描述。
// 双端共用：客户端用它构建设置面板，服务端用它构建管理员面板。
// 本模块零 LeviLamina 依赖，可 host 单测。

enum class PanelElementType {
    Header,   // 分组标题（只读文本）
    Label,    // 普通说明文本（只读）
    Divider,  // 分隔线
    Toggle,   // 开关，绑定布尔配置项
    Dropdown, // 下拉选择，绑定索引型配置项
    Slider,   // 数值滑块
    Input,    // 文本输入（如按键名）
};

struct PanelDropdownOption {
    std::string value;   // 语义值，写入配置时使用
    std::string textKey; // i18n 键
};

struct PanelElement {
    PanelElementType type = PanelElementType::Label;

    // 交互控件必填：既作为表单元素的 name，也作为配置键。
    std::string key;
    // i18n 键：header/label 用作正文，交互控件用作标签。
    std::string textKey;
    std::string placeholder;

    std::vector<PanelDropdownOption> options;

    double min = 0.0;
    double max = 0.0;
    double step = 0.0;
    double defaultNumber = 0.0;
    bool defaultBool = false;
    std::string defaultString;
};

struct PanelDefinition {
    int schemaVersion = 0;
    std::string id;
    std::string titleKey;
    std::string submitButtonKey;
    std::vector<PanelElement> elements;

    // 解析面板定义。返回 nullopt 时 error 说明原因（整体不可用，调用方应回落到内置默认）。
    // 单个元素不合法时只丢弃该元素，不影响整份定义（详见实现）。
    static std::optional<PanelDefinition> parse(std::string_view json, std::string& error);
};

// 当前支持的 schema 版本；不匹配的定义整体拒绝，避免按错误语义解析。
inline constexpr int kPanelSchemaVersion = 1;

} // namespace vc::ui
