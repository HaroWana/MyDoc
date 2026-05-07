add_library(mondoc_ui STATIC
    main_window.cpp
    schema_dock_widget.cpp
)

target_include_directories(mondoc_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_ui PUBLIC cxx_std_20)

target_link_libraries(mondoc_ui
    PUBLIC Qt6::Widgets mondoc_services mondoc_domain
    PRIVATE stduuid)
