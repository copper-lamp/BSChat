#include "client/input/Triggers.h"
#include <algorithm>
#include <utility>
namespace bsc::client::input {
TalkTrigger::TalkTrigger(TriggerCallback callback): callback_(std::move(callback)) {}
void TalkTrigger::onInput(const InputEvent& e) { if (e.pressed == active_) return; active_ = e.pressed; if (callback_) callback_(active_, e.atMs); }
PttTrigger::PttTrigger(uint32_t key, TriggerCallback callback): TalkTrigger(std::move(callback)), key_(key) {}
void PttTrigger::onKey(uint32_t key, bool pressed, int64_t atMs) { if (key == key_) onInput({pressed, key, atMs}); }

namespace {
// 噪声底尚未学到任何样本时用一个保守初值，避免头几帧被当成噪声直接抬开门限。
constexpr float kInitialNoiseFloor = 0.002F;
}

VadTrigger::VadTrigger(float threshold, int holdMs, TriggerCallback callback)
: VadTrigger(threshold, holdMs, 0, std::move(callback)) {}
VadTrigger::VadTrigger(float threshold, int holdMs, int onsetMs, TriggerCallback callback)
: TalkTrigger(std::move(callback)), threshold_(std::max(0.0F, threshold)) {
    options_.floor = kInitialNoiseFloor;
    options_.onsetMs = std::max(0, onsetMs);
    options_.holdMs = std::max(0, holdMs);
    noiseFloor_ = options_.floor;
}
float VadTrigger::startThreshold() const {
    return std::max(threshold_, noiseFloor_ * options_.startRatio);
}
float VadTrigger::stopThreshold() const {
    return std::max(threshold_ * options_.stopFactor, noiseFloor_ * options_.stopRatio);
}
void VadTrigger::onLevel(float rms, int64_t atMs) {
    const float level = std::max(0.0F, rms);
    // 噪声底：向下快、向上慢。向上必须慢，否则长句讲话会把门限一路抬到人声之上而再也停不下来。
    const float alpha = level < noiseFloor_ ? options_.floorDown : options_.floorUp;
    noiseFloor_ += (level - noiseFloor_) * alpha;

    if (level >= startThreshold()) {
        belowSinceMs_ = -1;
        if (aboveSinceMs_ < 0) aboveSinceMs_ = atMs;
        if (!active() && atMs - aboveSinceMs_ >= options_.onsetMs) onInput({true, 0, atMs});
        return;
    }
    aboveSinceMs_ = -1;
    if (!active()) return;
    if (level < stopThreshold()) {
        if (belowSinceMs_ < 0) belowSinceMs_ = atMs;
        if (atMs - belowSinceMs_ >= options_.holdMs) onInput({false, 0, atMs});
    } else {
        belowSinceMs_ = -1;
    }
}
void VadTrigger::reset(int64_t atMs) {
    aboveSinceMs_ = -1;
    belowSinceMs_ = -1;
    onInput({false, 0, atMs});
}
} // namespace bsc::client::input
