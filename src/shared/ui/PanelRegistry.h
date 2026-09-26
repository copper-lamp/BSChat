#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "shared/ui/PanelDefinition.h"

namespace vc::ui {

// 内置面板定义的加载与检索。面板以只读 JSON 文件随模组发布，用 PanelDefinition::id 作为检索键。
// 零 LeviLamina 依赖，可 host 单测。
class PanelRegistry final {
public:
    // 从目录读取全部 *.json 面板定义。坏文件跳过并记录错误，不因单个坏文件整体失败；
    // 目录不存在时返回空列表并记录错误。结果按文件名排序，保证加载顺序稳定。
    static std::vector<PanelDefinition> loadFromDirectory(
        std::filesystem::path const& dir,
        std::string* error = nullptr
    );

    // 用目录内容整体替换当前已加载的定义。
    void load(std::filesystem::path const& dir, std::string* error = nullptr);

    // 按 id 查找；未命中返回 nullptr。
    PanelDefinition const* find(std::string const& id) const;

    std::vector<PanelDefinition> const& panels() const { return panels_; }
    std::size_t size() const { return panels_.size(); }
    void clear() { panels_.clear(); }

private:
    std::vector<PanelDefinition> panels_;
};

} // namespace vc::ui
