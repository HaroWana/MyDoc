add_library(mondoc_adapters_formats STATIC
    placeholder.cpp
)

target_include_directories(mondoc_adapters_formats PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_adapters_formats PUBLIC cxx_std_20)

target_link_libraries(mondoc_adapters_formats
    PUBLIC mondoc_domain libzip::zip pugixml::pugixml stduuid::stduuid)

# Formats is a pure C++ adapter and must not link any Qt component.
