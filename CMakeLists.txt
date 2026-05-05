cmake_minimum_required(VERSION 3.25)

project(MonDoc
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "In-source builds prohibited. Use cmake --preset <preset>.")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <expected>\nint main(){ std::expected<int,int> e{0}; (void)e; return 0; }"
    MONDOC_HAVE_STD_EXPECTED)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

if(NOT MONDOC_HAVE_STD_EXPECTED)
    find_package(tl-expected CONFIG REQUIRED)
endif()

# Subdirectories (added by later plans):
# add_subdirectory(src/domain)             # Plan 02
# add_subdirectory(src/adapters/storage)   # Plan 03
# add_subdirectory(src/app)                # Plan 04

enable_testing()
add_subdirectory(tests)
