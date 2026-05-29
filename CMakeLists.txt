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

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Pdf)
qt_standard_project_setup()
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

if(NOT MONDOC_HAVE_STD_EXPECTED)
    find_package(tl-expected CONFIG REQUIRED)
endif()

find_package(libzip CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)

# Subdirectories (added by later plans):
add_subdirectory(src/domain)
add_subdirectory(src/adapters/storage)
add_subdirectory(src/adapters/formats)
add_subdirectory(src/adapters/ai)
add_subdirectory(src/services)
add_subdirectory(src/ui)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
