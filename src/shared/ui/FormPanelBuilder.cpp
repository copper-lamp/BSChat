#include "shared/ui/FormPanelBuilder.h"

#include <type_traits>

#include "ll/api/i18n/I18n.h"

namespace vc::ui {

std::unique_ptr<ll::form::CustomForm> FormPanelBuilder::build(
    PanelDefinition const& definition,
    PanelValueReader const& readValue,
    PanelTextResolver const& resolveText
) {
    auto resolve = [&resolveText](std::string_view key) {
        return resolveText ? resolveText(key) : std::string(key);
    };

    auto form = std::make_unique<ll::form::CustomForm>(resolve(definition.titleKey));
    if (!definition.submitButtonKey.empty()) form->setSubmitButton(resolve(definition.submitButtonKey));

    for (auto const& control : buildFormPlan(definition, readValue, resolveText)) {
        switch (control.type) {
        case PanelElementType::Header:
            form->appendHeader(control.text);
            break;
        case PanelElementType::Label:
            form->appendLabel(control.text);
            break;
        case PanelElementType::Divider:
            form->appendDivider();
            break;
        case PanelElementType::Toggle:
            form->appendToggle(control.name, control.text, control.defaultBool);
            break;
        case PanelElementType::Dropdown:
            form->appendDropdown(control.name, control.text, control.options, control.defaultIndex);
            break;
        case PanelElementType::Slider:
            form->appendSlider(control.name, control.text, control.min, control.max, control.step, control.defaultNumber);
            break;
        case PanelElementType::Input:
            form->appendInput(control.name, control.text, control.placeholder, control.defaultString);
            break;
        }
    }

    return form;
}

PanelValues FormPanelBuilder::toPanelValues(ll::form::CustomFormResult const& result) {
    PanelValues values;
    if (!result) return values;
    for (auto const& [name, value] : *result) {
        std::visit(
            [&values, &name](auto const& typed) {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, std::monostate>) values[name] = std::monostate{};
                else if constexpr (std::is_same_v<T, std::string>) values[name] = typed;
                else if constexpr (std::is_integral_v<T>) values[name] = static_cast<uint64_t>(typed);
                else values[name] = static_cast<double>(typed);
            },
            value
        );
    }
    return values;
}

PanelTextResolver FormPanelBuilder::i18nResolver() {
    return [](std::string_view key) -> std::string {
        auto const translated = ll::i18n::getInstance().get(key, ll::i18n::getDefaultLocaleCode());
        return translated.empty() ? std::string(key) : std::string(translated);
    };
}

} // namespace vc::ui
