#include "shared/ui/PanelDefinition.h"

#include <utility>

#include <nlohmann/json.hpp>

namespace bsc::ui {
namespace {

using Json = nlohmann::json;

bool readString(Json const& object, char const* field, std::string& out) {
    auto it = object.find(field);
    if (it == object.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return !out.empty();
}

std::optional<PanelElementType> elementTypeFromString(std::string const& name) {
    if (name == "header") return PanelElementType::Header;
    if (name == "label") return PanelElementType::Label;
    if (name == "divider") return PanelElementType::Divider;
    if (name == "toggle") return PanelElementType::Toggle;
    if (name == "dropdown") return PanelElementType::Dropdown;
    if (name == "slider") return PanelElementType::Slider;
    if (name == "input") return PanelElementType::Input;
    return std::nullopt;
}

// 单个元素不合法只丢弃该元素：面板是渐进增强的，坏掉一条不应让整页不可用。
std::optional<PanelElement> parseElement(Json const& node) {
    if (!node.is_object()) return std::nullopt;

    std::string typeName;
    if (!readString(node, "type", typeName)) return std::nullopt;
    auto type = elementTypeFromString(typeName);
    if (!type) return std::nullopt; // 未知类型：兼容更新版本的定义文件，直接跳过

    PanelElement element;
    element.type = *type;

    if (element.type == PanelElementType::Divider) return element;

    if (!readString(node, "text", element.textKey)) return std::nullopt;

    if (element.type == PanelElementType::Header || element.type == PanelElementType::Label) return element;

    // 以下为交互控件：必须绑定配置键
    if (!readString(node, "key", element.key)) return std::nullopt;

    switch (element.type) {
    case PanelElementType::Toggle: {
        if (auto it = node.find("default"); it != node.end() && it->is_boolean()) {
            element.defaultBool = it->get<bool>();
        }
        return element;
    }
    case PanelElementType::Dropdown: {
        auto it = node.find("options");
        if (it == node.end() || !it->is_array() || it->empty()) return std::nullopt;
        for (auto const& optionNode : *it) {
            if (!optionNode.is_object()) return std::nullopt;
            PanelDropdownOption option;
            if (!readString(optionNode, "value", option.value)) return std::nullopt;
            if (!readString(optionNode, "text", option.textKey)) return std::nullopt;
            element.options.push_back(std::move(option));
        }
        return element;
    }
    case PanelElementType::Slider: {
        auto minIt = node.find("min");
        auto maxIt = node.find("max");
        if (minIt == node.end() || !minIt->is_number() || maxIt == node.end() || !maxIt->is_number()) {
            return std::nullopt;
        }
        element.min = minIt->get<double>();
        element.max = maxIt->get<double>();
        if (!(element.max > element.min)) return std::nullopt;
        if (auto it = node.find("step"); it != node.end() && it->is_number()) {
            element.step = it->get<double>();
            if (element.step < 0.0) return std::nullopt;
        }
        if (auto it = node.find("default"); it != node.end() && it->is_number()) {
            element.defaultNumber = it->get<double>();
        }
        // 越界默认值收敛到区间内，避免弹出即非法
        if (element.defaultNumber < element.min) element.defaultNumber = element.min;
        if (element.defaultNumber > element.max) element.defaultNumber = element.max;
        return element;
    }
    case PanelElementType::Input: {
        if (auto it = node.find("placeholder"); it != node.end() && it->is_string()) {
            element.placeholder = it->get<std::string>();
        }
        if (auto it = node.find("default"); it != node.end() && it->is_string()) {
            element.defaultString = it->get<std::string>();
        }
        return element;
    }
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<PanelDefinition> PanelDefinition::parse(std::string_view json, std::string& error) {
    auto parsed = Json::parse(json.begin(), json.end(), nullptr, false);
    if (parsed.is_discarded()) {
        error = "panel definition is not valid JSON";
        return std::nullopt;
    }
    if (!parsed.is_object()) {
        error = "panel definition must be a JSON object";
        return std::nullopt;
    }

    PanelDefinition definition;
    if (auto it = parsed.find("schemaVersion"); it != parsed.end() && it->is_number_integer()) {
        definition.schemaVersion = it->get<int>();
    }
    if (definition.schemaVersion != kPanelSchemaVersion) {
        error = "unsupported panel schemaVersion " + std::to_string(definition.schemaVersion);
        return std::nullopt;
    }
    if (!readString(parsed, "id", definition.id)) {
        error = "panel definition requires a non-empty id";
        return std::nullopt;
    }
    if (!readString(parsed, "title", definition.titleKey)) {
        error = "panel definition requires a non-empty title";
        return std::nullopt;
    }
    if (auto it = parsed.find("submitButton"); it != parsed.end() && it->is_string()) {
        definition.submitButtonKey = it->get<std::string>();
    }

    auto elementsIt = parsed.find("elements");
    if (elementsIt == parsed.end() || !elementsIt->is_array()) {
        error = "panel definition requires an elements array";
        return std::nullopt;
    }
    for (auto const& node : *elementsIt) {
        if (auto element = parseElement(node)) definition.elements.push_back(std::move(*element));
    }
    if (definition.elements.empty()) {
        error = "panel definition has no usable elements";
        return std::nullopt;
    }
    return definition;
}

} // namespace bsc::ui
