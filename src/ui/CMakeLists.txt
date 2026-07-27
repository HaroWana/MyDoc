add_library(mondoc_ui STATIC
    about_dialog.cpp
    ai_field_detect_worker.cpp
    ai_fill_worker.cpp
    ai_refine_worker.cpp
    chat_pane.cpp
    edit_field_command.cpp
    export_dialog.cpp
    field_form_pane.cpp
    fill_session_view.cpp
    import_conflict_dialog.cpp
    main_window.cpp
    region_mark_viewer.cpp
    resume_banner.cpp
    schema_dock_widget.cpp
    settings_dialog.cpp
    source_doc_pane.cpp
)

target_include_directories(mondoc_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_ui PUBLIC cxx_std_20)

target_link_libraries(mondoc_ui
    PUBLIC Qt6::Widgets Qt6::Pdf mondoc_services mondoc_adapters_ai mondoc_domain
    PRIVATE stduuid)
