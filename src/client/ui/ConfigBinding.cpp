#include "client/ui/ConfigBinding.h"

namespace vc::client::ui {
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
        || key == "subtitleEnabled" || key == "maxSubtitleLines" || key == "hudEnabled";
}

PanelValue ConfigBinding::read(config::ClientConfig const& config, std::string const& key) {
    if (key == "voiceEnabled") return flag(config.voiceEnabled);
    if (key == "talkMode") return talkModeOf(config);
    if (key == "captureEnabled") return flag(config.captureEnabled);
    if (key == "playbackVolume") return static_cast<double>(config.playbackVolume) * kVolumeMax;
    if (key == "subtitleEnabled") return flag(config.subtitleEnabled);
    if (key == "maxSubtitleLines") return static_cast<double>(config.maxSubtitleLines);
    if (key == "hudEnabled") return flag(config.hudEnabled);
    return std::monostate{};
}

std::size_t ConfigBinding::apply(config::ClientConfig& config, PanelValues const& values) {
    std::size_t applied = 0;
    for (auto const& [key, value] : values) {
        bool accepted = false;

        if (key == "talkMode") {
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
    return applied;
}

} // namespace vc::client::ui
