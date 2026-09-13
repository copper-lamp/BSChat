#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/protocol/Message.h"
#include "server/session/PlayerSession.h"

namespace vc::server {

// protocol::PlayerId（std::array<uint8_t,16>）无标准 hash，自备 FNV-1a 风格哈希
struct PlayerIdHash {
    size_t operator()(const protocol::PlayerId& id) const noexcept {
        size_t h = 1469598103934665603ull;
        for (uint8_t b : id) {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }
};

// 服务端会话登记表：玩家 UUID → PlayerSession。
// 零 LeviLamina 依赖。
//
// 线程模型：
//  - 主线程：addSession / removeSession（玩家加入/离开事件）；
//  - 音频线程：snapshot() 取快照后遍历 pollFrame；
//  - 网络线程：find() 查找会话后 pushAudio。
// 会话以 shared_ptr 持有：音频线程持有快照引用期间即使被移除也不会悬垂。
class SessionManager {
public:
    using SessionPtr = std::shared_ptr<PlayerSession>;

    SessionManager() = default;

    // 玩家加入：创建会话。已存在则覆盖（复位旧会话）。
    void addSession(const protocol::PlayerId& id, PlayerSession::Options options);

    // 玩家离开：移除会话。
    void removeSession(const protocol::PlayerId& id);

    // 查找会话；返回的 shared_ptr 在持有期间保活。
    SessionPtr find(const protocol::PlayerId& id) const;

    // 音频线程：取全部会话快照（并发安全）。
    std::vector<SessionPtr> snapshot() const;

    size_t size() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<protocol::PlayerId, SessionPtr, PlayerIdHash> sessions_;
};

} // namespace vc::server
