#include "client/input/Triggers.h"
#include <algorithm>
#include <utility>
namespace bsc::client::input {
TalkTrigger::TalkTrigger(TriggerCallback callback): callback_(std::move(callback)) {}
void TalkTrigger::onInput(const InputEvent& e) { if (e.pressed == active_) return; active_ = e.pressed; if (callback_) callback_(active_, e.atMs); }
PttTrigger::PttTrigger(uint32_t key, TriggerCallback callback): TalkTrigger(std::move(callback)), key_(key) {}
void PttTrigger::onKey(uint32_t key, bool pressed, int64_t atMs) { if (key == key_) onInput({pressed, key, atMs}); }
VadTrigger::VadTrigger(float threshold, int holdMs, TriggerCallback callback): TalkTrigger(std::move(callback)), threshold_(std::max(0.0F, threshold)), holdMs_(std::max(0, holdMs)) {}
void VadTrigger::onLevel(float rms, int64_t atMs) { if (rms >= threshold_) { belowSinceMs_ = -1; onInput({true, 0, atMs}); } else if (active()) { if (belowSinceMs_ < 0) belowSinceMs_ = atMs; if (atMs - belowSinceMs_ >= holdMs_) onInput({false, 0, atMs}); } }
void VadTrigger::reset(int64_t atMs) { belowSinceMs_ = -1; onInput({false, 0, atMs}); }
} // namespace bsc::client::input
