#pragma once
#include <cstdint>
#include <functional>
#include <utility>
namespace vc::client::input {
class Shortcut final { public: using Callback=std::function<void()>; explicit Shortcut(uint32_t key, Callback cb={}): key_(key), callback_(std::move(cb)) {} bool handle(uint32_t key, bool pressed) { if (key != key_ || !pressed || fired_) return false; fired_=true; if(callback_) callback_(); return true; } void release(uint32_t key) { if(key==key_) fired_=false; } private: uint32_t key_; Callback callback_; bool fired_=false; };
}
