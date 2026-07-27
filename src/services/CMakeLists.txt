add_library(mondoc_services STATIC
    template_service.cpp
    fill_session_service.cpp
)

target_include_directories(mondoc_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_services PUBLIC cxx_std_23)

target_link_libraries(mondoc_services
    PUBLIC mondoc_domain
    PRIVATE mondoc_adapters_formats mondoc_adapters_ai stduuid)

# Services are pure C++ and must not link any Qt component.
