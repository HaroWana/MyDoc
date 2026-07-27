find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(mondoc_adapters_ai STATIC
    llm_config.cpp
    llm_client.cpp
    ai_fill_pipeline.cpp
    ai_field_detector.cpp
    detail/ai_json.cpp
)

target_include_directories(mondoc_adapters_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_adapters_ai PUBLIC cxx_std_23)

target_link_libraries(mondoc_adapters_ai
    PUBLIC mondoc_domain
    PRIVATE httplib::httplib nlohmann_json::nlohmann_json)

# AI adapter is pure C++ and must not link any Qt component.
