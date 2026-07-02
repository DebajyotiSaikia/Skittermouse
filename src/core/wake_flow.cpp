#include "core/wake_flow.h"

namespace sm::core {

void WakeFlow::start(const PeerId& target, uint64_t now_ms, uint64_t timeoutMs) {
    status_ = Status::Waking;
    target_ = target;
    start_ = now_ms;
    timeout_ = timeoutMs;
}

WakeFlow::Status WakeFlow::update(uint64_t now_ms, bool targetOnline) {
    if (status_ != Status::Waking) return status_;
    // Reconnection wins over the deadline (checked first), so a machine that returns
    // right at the timeout boundary is reported as Connected, not TimedOut.
    if (targetOnline) {
        status_ = Status::Connected;
    } else if (now_ms >= start_ && now_ms - start_ >= timeout_) {
        status_ = Status::TimedOut;
    }
    return status_;
}

void WakeFlow::reset() {
    status_ = Status::Idle;
    target_.clear();
    start_ = 0;
    timeout_ = 0;
}

} // namespace sm::core
