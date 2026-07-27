add_library(mondoc_adapters_storage STATIC
    sqlite_connection.cpp
    migrations.cpp
    sqlite_template_repository.cpp
    sqlite_fill_session_repository.cpp
)

target_include_directories(mondoc_adapters_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_adapters_storage PUBLIC cxx_std_20)

target_link_libraries(mondoc_adapters_storage
    PUBLIC mondoc_domain SQLiteCpp
    PRIVATE stduuid)

# Storage is a pure C++ adapter and must not link any Qt component.
