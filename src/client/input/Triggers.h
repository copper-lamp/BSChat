#pragma once

#include <cstdint>
#include <functional>

namespace vc::client::input {

struct InputEvent { bool pressed = false; uint32_t key = 0; int64_t atMs = 0; };
using TriggerCallback = std::function<void(bool active, int64_t atMs)>;

class TalkTrigger {
public:
    explicit TalkTrigger(TriggerCallback callback = {});
    void onInput(const InputEvent& event);
    bool active() const { return active_; }
private: TriggerCallback callback_; bool active_ = false;
};

class PttTrigger final : public TalkTrigger {
public: explicit PttTrigger(uint32_t key, TriggerCallback callback = {});
    void onKey(uint32_t key, bool pressed, int64_t atMs);
private: uint32_t key_;
};

class VadTrigger final : public TalkTrigger {
public: explicit VadTrigger(float threshold = 0.02F, int holdMs = 180, TriggerCallback callback = {});
    void onLevel(float rms, int64_t atMs);
    void reset(int64_t atMs);
private: float threshold_; int holdMs_; int64_t belowSinceMs_ = -1;
};

} // namespace vc::client::input
