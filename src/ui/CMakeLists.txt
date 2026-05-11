add_library(mondoc_ui STATIC
    ai_fill_worker.cpp
    chat_pane.cpp
    edit_field_command.cpp
    export_dialog.cpp
    field_form_pane.cpp
    fill_session_view.cpp
    main_window.cpp
    resume_banner.cpp
    schema_dock_widget.cpp
    source_doc_pane.cpp
)

target_include_directories(mondoc_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_ui PUBLIC cxx_std_20)

target_link_libraries(mondoc_ui
    PUBLIC Qt6::Widgets mondoc_services mondoc_domain
    PRIVATE stduuid)
