add_executable(tests_ui_canvas_geometry test_canvas_geometry.cpp)
target_link_libraries(tests_ui_canvas_geometry
    PRIVATE mondoc_domain Catch2::Catch2WithMain)
target_include_directories(tests_ui_canvas_geometry
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ui)
add_test(NAME ui.canvas_geometry COMMAND tests_ui_canvas_geometry)
