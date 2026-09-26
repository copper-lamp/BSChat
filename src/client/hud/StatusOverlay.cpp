#include "client/hud/StatusOverlay.h"

namespace vc::client::hud {

AudioStatus StatusOverlay::status() const {
    if (!inputs_.inSession) return AudioStatus::Idle;
    if (inputs_.muted) return AudioStatus::Muted;
    if (inputs_.talking) return AudioStatus::Speaking;
    if (inputs_.playing) return AudioStatus::Playing;
    return AudioStatus::Silent;
}

std::string_view StatusOverlay::i18nKey(AudioStatus status) {
    switch (status) {
    case AudioStatus::Idle:
        return "voicechat.status.idle";
    case AudioStatus::Speaking:
        return "voicechat.status.speaking";
    case AudioStatus::Muted:
        return "voicechat.status.muted";
    case AudioStatus::Playing:
        return "voicechat.status.playing";
    case AudioStatus::Silent:
        return "voicechat.status.silent";
    }
    return "voicechat.status.idle";
}

} // namespace vc::client::hud
