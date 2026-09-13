#include "server/session/SessionManager.h"

#include <utility>

namespace vc::server {

void SessionManager::addSession(const protocol::PlayerId& id, PlayerSession::Options options) {
    std::lock_guard lock(mutex_);
    sessions_[id] = std::make_shared<PlayerSession>(id, options);
}

void SessionManager::removeSession(const protocol::PlayerId& id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(id);
}

SessionManager::SessionPtr SessionManager::find(const protocol::PlayerId& id) const {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second;
}

std::vector<SessionManager::SessionPtr> SessionManager::snapshot() const {
    std::lock_guard lock(mutex_);
    std::vector<SessionPtr> out;
    out.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        (void)id;
        out.push_back(session);
    }
    return out;
}

size_t SessionManager::size() const {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

void SessionManager::clear() {
    std::lock_guard lock(mutex_);
    sessions_.clear();
}

} // namespace vc::server
