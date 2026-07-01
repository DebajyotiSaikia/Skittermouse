add_executable(skittermouse_tests
    test_main.cpp
    event_types_tests.cpp
    ownership_state_tests.cpp
    server_election_tests.cpp
    e2e_mesh_tests.cpp
)
target_link_libraries(skittermouse_tests PRIVATE skittermouse_core)

add_test(NAME skittermouse_tests COMMAND skittermouse_tests)
