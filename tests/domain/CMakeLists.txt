add_executable(tests_domain_expected test_expected_bridge.cpp)
target_link_libraries(tests_domain_expected
    PRIVATE mondoc_domain Catch2::Catch2WithMain)
add_test(NAME domain.expected COMMAND tests_domain_expected)

add_executable(tests_domain_types test_types.cpp)
target_link_libraries(tests_domain_types
    PRIVATE mondoc_domain Catch2::Catch2WithMain)
add_test(NAME domain.types COMMAND tests_domain_types)

add_executable(tests_domain_ports test_ports.cpp)
target_link_libraries(tests_domain_ports
    PRIVATE mondoc_domain Catch2::Catch2WithMain)
add_test(NAME domain.ports COMMAND tests_domain_ports)
