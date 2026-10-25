#include "client/hud/StatusOverlay.h"

namespace bsc::client::hud {

AudioStatus StatusOverlay::status() const {
    if (!inputs_.inSession) return AudioStatus::Idle;
    if (inputs_.muted) return AudioStatus::Muted;
    if (inputs_.talking) return AudioStatus::Speaking;
    if (inputs_.playing) return AudioStatus::Playing;
    return AudioStatus::Silent;
}

} // namespace bsc::client::hud
