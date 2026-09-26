#include "client/hud/StatusOverlay.h"

namespace bsc::client::hud {

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
        return "bschat.status.idle";
    case AudioStatus::Speaking:
        return "bschat.status.speaking";
    case AudioStatus::Muted:
        return "bschat.status.muted";
    case AudioStatus::Playing:
        return "bschat.status.playing";
    case AudioStatus::Silent:
        return "bschat.status.silent";
    }
    return "bschat.status.idle";
}

} // namespace bsc::client::hud
