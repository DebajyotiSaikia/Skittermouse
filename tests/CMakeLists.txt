add_executable(qqmouse_tests
    test_main.cpp
    event_types_tests.cpp
    ownership_state_tests.cpp
    server_election_tests.cpp
    e2e_mesh_tests.cpp
)
target_link_libraries(qqmouse_tests PRIVATE qqmouse_core)

add_test(NAME qqmouse_tests COMMAND qqmouse_tests)
