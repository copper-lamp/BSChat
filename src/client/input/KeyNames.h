#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bsc::client::input {

// 面板里用户填写的按键名与虚拟键码互转（LeviLamina 客户端 KeyInputEvent 用的是虚拟键码）。
// 只收录可以安全绑定的常用键：字母、数字、F1-F12、少量功能键；
// 不引入修饰键组合与多媒体键，避免配置文件里出现无法触发或互相干扰的绑定。
// 本模块零 LeviLamina 依赖，可 host 单测。

// 名称大小写不敏感，允许两端空白；未收录返回 nullopt。
std::optional<uint32_t> virtualKeyFromName(std::string_view name);

// 反向查询，返回规范名；未收录的键码返回空串（调用方据此只显示 placeholder）。
std::string nameFromVirtualKey(uint32_t virtualKey);

} // namespace bsc::client::input
