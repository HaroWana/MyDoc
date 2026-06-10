find_package(httplib CONFIG REQUIRED)

add_executable(tests_adapters_ai_llm_config test_llm_config.cpp)
target_link_libraries(tests_adapters_ai_llm_config
    PRIVATE mondoc_adapters_ai Catch2::Catch2WithMain)
add_test(NAME ai.llm_config COMMAND tests_adapters_ai_llm_config)
set_tests_properties(ai.llm_config PROPERTIES LABELS phase03)

add_executable(tests_adapters_ai_error test_llm_error_classification.cpp)
target_link_libraries(tests_adapters_ai_error
    PRIVATE mondoc_adapters_ai httplib::httplib Catch2::Catch2WithMain)
add_test(NAME ai.error_classification COMMAND tests_adapters_ai_error)
set_tests_properties(ai.error_classification PROPERTIES LABELS phase03)

add_executable(tests_adapters_ai_pass1 test_pass1_validation.cpp)
target_link_libraries(tests_adapters_ai_pass1
    PRIVATE mondoc_adapters_ai Catch2::Catch2WithMain)
add_test(NAME ai.pass1_validation COMMAND tests_adapters_ai_pass1)
set_tests_properties(ai.pass1_validation PROPERTIES LABELS phase03)

add_executable(tests_adapters_ai_pipeline test_ai_fill_pipeline.cpp)
target_link_libraries(tests_adapters_ai_pipeline
    PRIVATE mondoc_adapters_ai Catch2::Catch2WithMain)
add_test(NAME ai.pipeline COMMAND tests_adapters_ai_pipeline)
set_tests_properties(ai.pipeline PROPERTIES LABELS phase03)

add_executable(tests_adapters_ai_confidence test_confidence_parse.cpp)
target_link_libraries(tests_adapters_ai_confidence
    PRIVATE mondoc_adapters_ai Catch2::Catch2WithMain)
add_test(NAME ai.confidence_parse COMMAND tests_adapters_ai_confidence)
set_tests_properties(ai.confidence_parse PROPERTIES LABELS phase03)

add_executable(tests_adapters_ai_field_detector test_ai_field_detector.cpp)
target_link_libraries(tests_adapters_ai_field_detector
    PRIVATE mondoc_adapters_ai Catch2::Catch2WithMain)
add_test(NAME ai.field_detector COMMAND tests_adapters_ai_field_detector)
set_tests_properties(ai.field_detector PROPERTIES LABELS phase06)
