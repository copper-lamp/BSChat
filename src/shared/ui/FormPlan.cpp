#include "shared/ui/FormPlan.h"

#include <utility>

#include <nlohmann/json.hpp>

namespace bsc::ui {
namespace {

using Json = nlohmann::json;

bool isInteractive(PanelElementType type) {
    switch (type) {
    case PanelElementType::Toggle:
    case PanelElementType::Dropdown:
    case PanelElementType::Slider:
    case PanelElementType::Input:
        return true;
    default:
        return false;
    }
}

} // namespace

std::vector<FormControlPlan> buildFormPlan(
    PanelDefinition const& definition,
    PanelValueReader const& readValue,
    PanelTextResolver const& resolveText
) {
    std::vector<FormControlPlan> plan;
    plan.reserve(definition.elements.size());

    for (auto const& element : definition.elements) {
        FormControlPlan control;
        control.type = element.type;
        control.text = resolveText ? resolveText(element.textKey) : std::string(element.textKey);

        if (!isInteractive(element.type)) {
            plan.push_back(std::move(control)); // header/label/divider：只有文案
            continue;
        }

        control.name = element.key;
        control.placeholder = element.placeholder;
        PanelValue const current = readValue ? readValue(element.key) : PanelValue{};

        switch (element.type) {
        case PanelElementType::Toggle: {
            if (auto const* number = std::get_if<uint64_t>(&current)) control.defaultBool = *number != 0;
            else if (auto const* real = std::get_if<double>(&current)) control.defaultBool = *real != 0.0;
            else control.defaultBool = element.defaultBool;
            break;
        }
        case PanelElementType::Dropdown: {
            for (auto const& option : element.options) {
                control.options.push_back(resolveText ? resolveText(option.textKey) : std::string(option.textKey));
            }
            control.defaultIndex = 0;
            if (auto const* text = std::get_if<std::string>(&current)) {
                for (std::size_t i = 0; i < element.options.size(); ++i) {
                    if (element.options[i].value == *text) {
                        control.defaultIndex = i;
                        break;
                    }
                }
            } else if (auto const* number = std::get_if<uint64_t>(&current)) {
                if (*number < element.options.size()) control.defaultIndex = static_cast<std::size_t>(*number);
            }
            break;
        }
        case PanelElementType::Slider: {
            control.min = element.min;
            control.max = element.max;
            control.step = element.step > 0.0 ? element.step : 1.0;
            double value = element.defaultNumber;
            if (auto const* real = std::get_if<double>(&current)) value = *real;
            else if (auto const* number = std::get_if<uint64_t>(&current)) value = static_cast<double>(*number);
            if (value < control.min) value = control.min;
            if (value > control.max) value = control.max;
            control.defaultNumber = value;
            break;
        }
        case PanelElementType::Input: {
            if (auto const* text = std::get_if<std::string>(&current)) control.defaultString = *text;
            else control.defaultString = element.defaultString;
            break;
        }
        default:
            break;
        }

        plan.push_back(std::move(control));
    }

    return plan;
}

PanelValues parseFormResponse(
    PanelDefinition const& definition,
    std::string_view payloadJson,
    std::string* error
) {
    PanelValues values;
    auto parsed = Json::parse(payloadJson.begin(), payloadJson.end(), nullptr, false);
    if (parsed.is_discarded()) {
        if (error) *error = "panel response is not valid JSON";
        return values;
    }
    if (!parsed.is_array()) {
        if (error) *error = "panel response is not a JSON array";
        return values;
    }

    for (std::size_t i = 0; i < definition.elements.size(); ++i) {
        auto const& element = definition.elements[i];
        if (!isInteractive(element.type)) continue;
        if (i >= parsed.size()) break;
        auto const& node = parsed[i];

        switch (element.type) {
        case PanelElementType::Toggle: {
            if (node.is_boolean()) values[element.key] = static_cast<uint64_t>(node.get<bool>() ? 1 : 0);
            else if (node.is_number_integer()) values[element.key] = static_cast<uint64_t>(node.get<int64_t>() != 0 ? 1 : 0);
            break;
        }
        case PanelElementType::Dropdown: {
            if (node.is_number_integer()) {
                int64_t const index = node.get<int64_t>();
                if (index >= 0 && index < static_cast<int64_t>(element.options.size())) {
                    values[element.key] = element.options[static_cast<std::size_t>(index)].value;
                }
            } else if (node.is_string()) {
                values[element.key] = node.get<std::string>();
            }
            break;
        }
        case PanelElementType::Slider: {
            if (node.is_number()) values[element.key] = node.get<double>();
            break;
        }
        case PanelElementType::Input: {
            if (node.is_string()) values[element.key] = node.get<std::string>();
            break;
        }
        default:
            break;
        }
    }

    return values;
}

} // namespace bsc::ui
