#include "client/input/KeyNames.h"

#include <array>
#include <cctype>
#include <string_view>

namespace vc::client::input {
namespace {

struct KeyName {
    std::string_view name; // 规范名
    uint32_t         virtualKey;
};

// 规范名表：反向查询也只返回这里列出的名字。
constexpr std::array<KeyName, 19> kNamedKeys{{
    {"SPACE", 0x20},
    {"TAB", 0x09},
    {"ENTER", 0x0D},
    {"ESCAPE", 0x1B},
    {"BACKSPACE", 0x08},
    {"INSERT", 0x2D},
    {"DELETE", 0x2E},
    {"HOME", 0x24},
    {"END", 0x23},
    {"PAGEUP", 0x21},
    {"PAGEDOWN", 0x22},
    {"UP", 0x26},
    {"DOWN", 0x28},
    {"LEFT", 0x25},
    {"RIGHT", 0x27},
    {"SHIFT", 0x10},
    {"CONTROL", 0x11},
    {"ALT", 0x12},
    {"CAPSLOCK", 0x14},
}};

struct KeyAlias {
    std::string_view alias;
    std::string_view canonical;
};

constexpr std::array<KeyAlias, 4> kAliases{{
    {"ESC", "ESCAPE"},
    {"CTRL", "CONTROL"},
    {"RETURN", "ENTER"},
    {"SPACEBAR", "SPACE"},
}};

constexpr std::string_view kFunctionKeyPrefix = "F";
constexpr uint32_t         kFunctionKeyFirst  = 0x70; // VK_F1
constexpr uint32_t         kFunctionKeyLast   = 0x7B; // VK_F12

std::string normalize(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char const ch : name) {
        auto const unsignedCh = static_cast<unsigned char>(ch);
        if (std::isspace(unsignedCh)) continue;
        result.push_back(static_cast<char>(std::toupper(unsignedCh)));
    }
    return result;
}

std::optional<uint32_t> namedKey(std::string_view normalized) {
    for (auto const& entry : kNamedKeys) {
        if (entry.name == normalized) return entry.virtualKey;
    }
    for (auto const& entry : kAliases) {
        if (entry.alias == normalized) return namedKey(entry.canonical);
    }
    return std::nullopt;
}

std::optional<uint32_t> functionKey(std::string_view normalized) {
    if (normalized.size() < 2 || normalized.front() != kFunctionKeyPrefix.front()) return std::nullopt;
    uint32_t index = 0;
    for (std::size_t i = 1; i < normalized.size(); ++i) {
        char const ch = normalized[i];
        if (ch < '0' || ch > '9') return std::nullopt;
        index = index * 10 + static_cast<uint32_t>(ch - '0');
    }
    if (index < 1 || index > (kFunctionKeyLast - kFunctionKeyFirst + 1)) return std::nullopt;
    return kFunctionKeyFirst + index - 1;
}

} // namespace

std::optional<uint32_t> virtualKeyFromName(std::string_view name) {
    auto const normalized = normalize(name);
    if (normalized.empty()) return std::nullopt;

    if (normalized.size() == 1) {
        char const ch = normalized.front();
        if (ch >= 'A' && ch <= 'Z') return static_cast<uint32_t>(ch);
        if (ch >= '0' && ch <= '9') return static_cast<uint32_t>(ch);
        return std::nullopt;
    }

    if (auto key = functionKey(normalized)) return key;
    return namedKey(normalized);
}

std::string nameFromVirtualKey(uint32_t virtualKey) {
    if (virtualKey >= static_cast<uint32_t>('A') && virtualKey <= static_cast<uint32_t>('Z')) {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= static_cast<uint32_t>('0') && virtualKey <= static_cast<uint32_t>('9')) {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= kFunctionKeyFirst && virtualKey <= kFunctionKeyLast) {
        return "F" + std::to_string(virtualKey - kFunctionKeyFirst + 1);
    }
    for (auto const& entry : kNamedKeys) {
        if (entry.virtualKey == virtualKey) return std::string(entry.name);
    }
    return {};
}

} // namespace vc::client::input
