add_library(mondoc_domain STATIC
    mondoc/expected.hpp
    mondoc/id.hpp
    mondoc/error.hpp
    mondoc/util.hpp
    domain/field.hpp
    domain/template.hpp
    domain/source_ref.hpp
    domain/confidence.hpp
    domain/fill.hpp
    domain/fill_session.hpp
    domain/i_document_reader.hpp
    domain/i_template_repository.hpp
    domain/i_fill_session_repository.hpp
)

set_target_properties(mondoc_domain PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(mondoc_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_domain PUBLIC cxx_std_23)

# When std::expected is unavailable at compile time, fall back to tl::expected.
# The bridge header at mondoc/expected.hpp picks the include via __cpp_lib_expected.
if(NOT MONDOC_HAVE_STD_EXPECTED)
    target_link_libraries(mondoc_domain PUBLIC tl::expected)
endif()

# CRITICAL: do NOT link this target to any Qt component.
# Qt isolation is enforced by the absence of a Qt link dependency.
# CI also greps for stray Qt headers under src/domain/ as a tripwire (Plan 05).
