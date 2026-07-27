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

add_executable(tests_domain_field_location test_field_location.cpp)
target_link_libraries(tests_domain_field_location
    PRIVATE mondoc_domain Catch2::Catch2WithMain)
add_test(NAME domain.field_location COMMAND tests_domain_field_location)
set_tests_properties(domain.field_location PROPERTIES LABELS phase05)

add_executable(tests_domain_util test_util.cpp)
target_link_libraries(tests_domain_util
    PRIVATE mondoc_domain stduuid Catch2::Catch2WithMain)
add_test(NAME domain.util COMMAND tests_domain_util)
