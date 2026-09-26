#pragma once

#include <memory>
#include <string_view>

#include "ll/api/form/CustomForm.h"
#include "shared/ui/FormPlan.h"
#include "shared/ui/PanelDefinition.h"

namespace bsc::ui {

// 把 PanelDefinition 构造成 ll::form::CustomForm。依赖 ll::form，仅双端适配层使用，不进 host 单测。
// 定义 → 控件入参的映射与响应解析拆到 FormPlan（零 ll::form 依赖，可 host 单测）。
class FormPanelBuilder final {
public:
    // 用当前值填充默认值构建表单。CustomForm 不可拷贝/移动，故返回持有所有权。
    // text 经 resolveText 解析（未命中由 resolver 回落键名，见 i18nResolver）。
    static std::unique_ptr<ll::form::CustomForm> build(
        PanelDefinition const& definition,
        PanelValueReader const& readValue,
        PanelTextResolver const& resolveText
    );

    // 把 ll::form 的表单结果（服务端本地面板用 CustomForm::sendTo 拿到）转成 PanelValues。
    static PanelValues toPanelValues(ll::form::CustomFormResult const& result);

    // i18n 文案解析器：命中译文即用，未命中回落键名本身（便于在界面上看出漏配的键）。
    static PanelTextResolver i18nResolver();
};

} // namespace bsc::ui
