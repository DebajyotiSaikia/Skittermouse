#include "test_framework.h"

void run_event_types_tests();
void run_ownership_state_tests();
void run_server_election_tests();
void run_e2e_mesh_tests();

int main() {
    run_event_types_tests();
    run_ownership_state_tests();
    run_server_election_tests();
    run_e2e_mesh_tests();

    std::printf("\n%d checks, %d failure(s)\n", qqtest::g_checks, qqtest::g_failures);
    return qqtest::g_failures == 0 ? 0 : 1;
}
