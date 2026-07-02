#include "test_framework.h"

#include "core/wake_flow.h"

using sm::core::WakeFlow;

void run_wake_flow_tests() {
    using Status = WakeFlow::Status;

    // --- Idle until started ---------------------------------------------------
    {
        WakeFlow w;
        SM_CHECK(w.status() == Status::Idle);
        SM_CHECK(!w.isWaking());
        // update() on an Idle flow is a no-op.
        SM_CHECK(w.update(1000, true) == Status::Idle);
    }

    // --- Reconnect within the window -> Connected -----------------------------
    {
        WakeFlow w;
        w.start("DESKTOP-B", 1000, 30000);
        SM_CHECK(w.status() == Status::Waking);
        SM_CHECK(w.isWaking());
        SM_CHECK_EQ(w.target(), std::string("DESKTOP-B"));

        SM_CHECK(w.update(5000, false) == Status::Waking);  // still waking
        SM_CHECK(w.update(9000, true) == Status::Connected); // came back
        // Terminal state is sticky even if the peer flaps offline again.
        SM_CHECK(w.update(9500, false) == Status::Connected);
    }

    // --- No reconnect -> TimedOut at the deadline -----------------------------
    {
        WakeFlow w;
        w.start("MAC-A", 0, 30000);
        SM_CHECK(w.update(29999, false) == Status::Waking);  // just before
        SM_CHECK(w.update(30000, false) == Status::TimedOut); // exactly at deadline
        SM_CHECK(w.update(60000, true) == Status::TimedOut);  // sticky afterwards
    }

    // --- Reconnect exactly at the boundary beats the timeout ------------------
    {
        WakeFlow w;
        w.start("PC", 100, 500);
        SM_CHECK(w.update(600, true) == Status::Connected);
    }

    // --- reset() returns to Idle and clears the target ------------------------
    {
        WakeFlow w;
        w.start("X", 0, 1000);
        w.update(2000, false); // TimedOut
        SM_CHECK(w.status() == Status::TimedOut);
        w.reset();
        SM_CHECK(w.status() == Status::Idle);
        SM_CHECK(w.target().empty());
    }

    // --- Restart after a terminal state reuses the flow -----------------------
    {
        WakeFlow w;
        w.start("A", 0, 100);
        SM_CHECK(w.update(200, false) == Status::TimedOut);
        w.start("B", 1000, 5000); // reused for a new attempt
        SM_CHECK(w.status() == Status::Waking);
        SM_CHECK_EQ(w.target(), std::string("B"));
        SM_CHECK(w.update(1200, true) == Status::Connected);
    }
}
