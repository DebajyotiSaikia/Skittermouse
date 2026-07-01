#include "core/server_election.h"

#include <algorithm>
#include <utility>

namespace sm::core {

bool ServerElection::contains(const std::vector<PeerId>& v, const PeerId& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

void ServerElection::addUnique(std::vector<PeerId>& v, const PeerId& id) {
    if (!contains(v, id))
        v.push_back(id);
}

void ServerElection::eraseValue(std::vector<PeerId>& v, const PeerId& id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

ServerElection::ServerElection(std::vector<PeerId> priority)
    : priority_(std::move(priority)) {}

void ServerElection::setPriority(std::vector<PeerId> priority) {
    priority_ = std::move(priority);
}

void ServerElection::addToPriority(const PeerId& id) {
    addUnique(priority_, id);
}

void ServerElection::removeFromPriority(const PeerId& id) {
    eraseValue(priority_, id);
}

bool ServerElection::isEligible(const PeerId& id) const {
    return contains(priority_, id);
}

void ServerElection::setOnline(const std::vector<PeerId>& online) {
    online_ = online;
}

void ServerElection::markOnline(const PeerId& id) {
    addUnique(online_, id);
}

void ServerElection::markOffline(const PeerId& id) {
    eraseValue(online_, id);
}

bool ServerElection::isOnline(const PeerId& id) const {
    return contains(online_, id);
}

std::optional<PeerId> ServerElection::currentServer() const {
    // Priority order is authoritative; return the first eligible machine that is
    // also online. For machine k to win, every earlier (higher-priority) entry
    // must be offline (Section 11.5).
    for (const auto& id : priority_) {
        if (contains(online_, id))
            return id;
    }
    return std::nullopt;
}

bool ServerElection::isServer(const PeerId& id) const {
    const std::optional<PeerId> server = currentServer();
    return server.has_value() && *server == id;
}

} // namespace sm::core
