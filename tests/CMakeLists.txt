add_library(tests_support INTERFACE)
target_include_directories(tests_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(domain)
add_subdirectory(adapters)
add_subdirectory(services)
