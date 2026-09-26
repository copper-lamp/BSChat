#include "client/ui/ConfigBinding.h"

#include "client/input/KeyNames.h"

namespace bsc::client::ui {
namespace {

constexpr double kVolumeMin = 0.0;
constexpr double kVolumeMax = 100.0;
constexpr double kSubtitleLinesMin = 1.0;
constexpr double kSubtitleLinesMax = 8.0;

constexpr char const* kTalkModeDisabled = "disabled";
constexpr char const* kTalkModePushToTalk = "pushToTalk";
constexpr char const* kTalkModeVoiceActivity = "voiceActivity";

uint64_t flag(bool value) { return value ? 1ull : 0ull; }

std::string talkModeOf(config::ClientConfig const& config) {
    if (!config.voiceEnabled) return kTalkModeDisabled;
    return config.vadEnabled ? kTalkModeVoiceActivity : kTalkModePushToTalk;
}

} // namespace

bool ConfigBinding::supported(std::string const& key) {
    return key == "voiceEnabled" || key == "talkMode" || key == "captureEnabled" || key == "playbackVolume"
        || key == "subtitleEnabled" || key == "maxSubtitleLines" || key == "hudEnabled" || key == "pttKeyName"
        || key == "settingsKeyName";
}

PanelValue ConfigBinding::read(config::ClientConfig const& config, std::string const& key) {
    if (key == "voiceEnabled") return flag(config.voiceEnabled);
    if (key == "talkMode") return talkModeOf(config);
    if (key == "captureEnabled") return flag(config.captureEnabled);
    if (key == "playbackVolume") return static_cast<double>(config.playbackVolume) * kVolumeMax;
    if (key == "subtitleEnabled") return flag(config.subtitleEnabled);
    if (key == "maxSubtitleLines") return static_cast<double>(config.maxSubtitleLines);
    if (key == "hudEnabled") return flag(config.hudEnabled);
    if (key == "pttKeyName" || key == "settingsKeyName") {
        // 当前键不在收录表里时返回 monostate，面板只显示 placeholder 而不塞一个错名字。
        auto name = input::nameFromVirtualKey(key == "pttKeyName" ? config.pttKey : config.settingsKey);
        if (name.empty()) return std::monostate{};
        return name;
    }
    return std::monostate{};
}

std::size_t ConfigBinding::apply(config::ClientConfig& config, PanelValues const& values) {
    // 两个按键名条目需要成对校验（不能绑同一个键），因此先统一收集再一次性落盘，
    // 否则 PanelValues 的遍历顺序会影响结果。
    struct KeyChange {
        bool     valid = false;
        uint32_t value = 0;
    };
    auto collectKeyChange = [&values](std::string const& name, uint32_t current) {
        KeyChange change;
        auto it = values.find(name);
        if (it == values.end()) return change;
        change.value = current;
        if (auto const* text = std::get_if<std::string>(&it->second)) {
            if (auto virtualKey = input::virtualKeyFromName(*text)) {
                change.value = *virtualKey;
                change.valid = true;
            }
        }
        return change;
    };
    KeyChange ptt = collectKeyChange("pttKeyName", config.pttKey);
    KeyChange settings = collectKeyChange("settingsKeyName", config.settingsKey);
    if (ptt.valid && settings.valid) {
        // 两个键都被改成同一个键，无法判断该保留哪个 → 两条都拒绝
        if (ptt.value == settings.value) {
            ptt.valid = false;
            settings.valid = false;
        }
    } else if (ptt.valid && ptt.value == config.settingsKey) {
        ptt.valid = false;
    } else if (settings.valid && settings.value == config.pttKey) {
        settings.valid = false;
    }

    std::size_t applied = 0;
    for (auto const& [key, value] : values) {
        bool accepted = false;

        if (key == "pttKeyName" || key == "settingsKeyName") {
            continue; // 已在上面成对处理
        } else if (key == "talkMode") {
            if (auto const* text = std::get_if<std::string>(&value)) {
                if (*text == kTalkModeDisabled) {
                    config.voiceEnabled = false;
                    accepted = true;
                } else if (*text == kTalkModePushToTalk) {
                    config.voiceEnabled = true;
                    config.vadEnabled = false;
                    accepted = true;
                } else if (*text == kTalkModeVoiceActivity) {
                    config.voiceEnabled = true;
                    config.vadEnabled = true;
                    accepted = true;
                }
            }
        } else if (key == "playbackVolume") {
            if (auto const* number = std::get_if<double>(&value); number && *number >= kVolumeMin && *number <= kVolumeMax) {
                config.playbackVolume = static_cast<float>(*number / kVolumeMax);
                accepted = true;
            }
        } else if (key == "maxSubtitleLines") {
            if (auto const* number = std::get_if<double>(&value);
                number && *number >= kSubtitleLinesMin && *number <= kSubtitleLinesMax) {
                config.maxSubtitleLines = static_cast<int>(*number + 0.5);
                accepted = true;
            }
        } else if (
            key == "voiceEnabled" || key == "captureEnabled" || key == "subtitleEnabled" || key == "hudEnabled"
        ) {
            if (auto const* number = std::get_if<uint64_t>(&value)) {
                bool const on = *number != 0;
                if (key == "voiceEnabled") config.voiceEnabled = on;
                else if (key == "captureEnabled") config.captureEnabled = on;
                else if (key == "subtitleEnabled") config.subtitleEnabled = on;
                else config.hudEnabled = on;
                accepted = true;
            }
        }

        if (accepted) ++applied;
    }

    if (ptt.valid) {
        config.pttKey = ptt.value;
        ++applied;
    }
    if (settings.valid) {
        config.settingsKey = settings.value;
        ++applied;
    }
    return applied;
}

} // namespace bsc::client::ui
