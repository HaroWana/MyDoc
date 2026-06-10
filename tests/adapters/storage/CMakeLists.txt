add_executable(tests_adapters_storage_migrations test_migrations.cpp)
target_link_libraries(tests_adapters_storage_migrations
    PRIVATE mondoc_adapters_storage Catch2::Catch2WithMain)
add_test(NAME storage.migrations COMMAND tests_adapters_storage_migrations)

add_executable(tests_adapters_storage_template_repository test_template_repository.cpp)
target_link_libraries(tests_adapters_storage_template_repository
    PRIVATE mondoc_adapters_storage Catch2::Catch2WithMain)
add_test(NAME storage.template_repo COMMAND tests_adapters_storage_template_repository)

add_executable(tests_adapters_storage_fill_session test_fill_session_repository.cpp)
target_link_libraries(tests_adapters_storage_fill_session
    PRIVATE mondoc_adapters_storage Catch2::Catch2WithMain)
add_test(NAME storage.fill_session COMMAND tests_adapters_storage_fill_session)
set_tests_properties(storage.fill_session PROPERTIES LABELS phase02)

add_test(NAME storage.fill_session_repo
         COMMAND tests_adapters_storage_fill_session
         "[adapters.storage][fill_session_repo][phase03]")
set_tests_properties(storage.fill_session_repo PROPERTIES LABELS phase03)

add_test(NAME storage.template_repo_ai
         COMMAND tests_adapters_storage_template_repository
         "[adapters.storage][template_repo][phase06]")
set_tests_properties(storage.template_repo_ai PROPERTIES LABELS phase06)
