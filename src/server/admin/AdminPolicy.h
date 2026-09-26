#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <shared_mutex>

namespace bsc::server::admin {

using PlayerId = std::array<std::uint8_t, 16>;

struct PlayerIdHash {
    std::size_t operator()(PlayerId const& id) const noexcept;
};

// Thread-safe in-memory admission and per-player gain policy.
class AdminPolicy {
public:
    AdminPolicy() = default;

    void setWhitelistEnabled(bool enabled);
    bool whitelistEnabled() const;

    void addWhitelist(PlayerId id);
    void removeWhitelist(PlayerId id);
    void addBlacklist(PlayerId id);
    void removeBlacklist(PlayerId id);
    bool isWhitelisted(PlayerId id) const;
    bool isBlacklisted(PlayerId id) const;

    // Blacklist always wins. If the whitelist is enabled, only its members pass.
    bool isAllowed(PlayerId id) const;

    // Gain is clamped to [0, 4]. Missing entries use defaultGain (1.0).
    void setGain(PlayerId id, float gain);
    void removeGain(PlayerId id);
    float gain(PlayerId id) const;

    std::vector<PlayerId> whitelist() const;
    std::vector<PlayerId> blacklist() const;
    std::vector<std::pair<PlayerId, float>> gains() const;
    void clear();

private:
    mutable std::shared_mutex mutex_;
    bool whitelistEnabled_ = false;
    std::unordered_set<PlayerId, PlayerIdHash> whitelist_;
    std::unordered_set<PlayerId, PlayerIdHash> blacklist_;
    std::unordered_map<PlayerId, float, PlayerIdHash> gains_;
};

} // namespace bsc::server::admin
