#include "server/admin/AdminPolicy.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <mutex>

namespace bsc::server::admin {

std::size_t PlayerIdHash::operator()(PlayerId const& id) const noexcept {
    std::size_t h = 1469598103934665603ull;
    for (auto byte : id) { h ^= byte; h *= 1099511628211ull; }
    return h;
}

void AdminPolicy::setWhitelistEnabled(bool enabled) { std::unique_lock lock(mutex_); whitelistEnabled_ = enabled; }
bool AdminPolicy::whitelistEnabled() const { std::shared_lock lock(mutex_); return whitelistEnabled_; }
void AdminPolicy::addWhitelist(PlayerId id) { std::unique_lock lock(mutex_); whitelist_.insert(id); }
void AdminPolicy::removeWhitelist(PlayerId id) { std::unique_lock lock(mutex_); whitelist_.erase(id); }
void AdminPolicy::addBlacklist(PlayerId id) { std::unique_lock lock(mutex_); blacklist_.insert(id); }
void AdminPolicy::removeBlacklist(PlayerId id) { std::unique_lock lock(mutex_); blacklist_.erase(id); }
bool AdminPolicy::isWhitelisted(PlayerId id) const { std::shared_lock lock(mutex_); return whitelist_.contains(id); }
bool AdminPolicy::isBlacklisted(PlayerId id) const { std::shared_lock lock(mutex_); return blacklist_.contains(id); }
bool AdminPolicy::isAllowed(PlayerId id) const {
    std::shared_lock lock(mutex_);
    return !blacklist_.contains(id) && (!whitelistEnabled_ || whitelist_.contains(id));
}
void AdminPolicy::setGain(PlayerId id, float value) {
    std::unique_lock lock(mutex_);
    if (!(value >= 0.0f)) value = 0.0f;
    gains_[id] = std::min(value, 4.0f);
}
void AdminPolicy::removeGain(PlayerId id) { std::unique_lock lock(mutex_); gains_.erase(id); }
float AdminPolicy::gain(PlayerId id) const {
    std::shared_lock lock(mutex_);
    auto it = gains_.find(id); return it == gains_.end() ? 1.0f : it->second;
}
std::vector<PlayerId> AdminPolicy::whitelist() const { std::shared_lock lock(mutex_); return {whitelist_.begin(), whitelist_.end()}; }
std::vector<PlayerId> AdminPolicy::blacklist() const { std::shared_lock lock(mutex_); return {blacklist_.begin(), blacklist_.end()}; }
std::vector<std::pair<PlayerId, float>> AdminPolicy::gains() const { std::shared_lock lock(mutex_); return {gains_.begin(), gains_.end()}; }
void AdminPolicy::clear() { std::unique_lock lock(mutex_); whitelist_.clear(); blacklist_.clear(); gains_.clear(); whitelistEnabled_ = false; }

} // namespace bsc::server::admin
