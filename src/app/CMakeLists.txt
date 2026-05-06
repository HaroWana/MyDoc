qt_add_executable(mondoc_app
    main.cpp
    composition_root.cpp
)

target_link_libraries(mondoc_app PRIVATE
    Qt6::Widgets
    mondoc_ui
    mondoc_services
    mondoc_adapters_storage
    mondoc_domain
)

set_target_properties(mondoc_app PROPERTIES
    OUTPUT_NAME mondoc
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
)
