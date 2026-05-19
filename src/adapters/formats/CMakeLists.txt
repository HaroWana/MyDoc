find_package(podofo CONFIG REQUIRED)

add_library(mondoc_adapters_formats STATIC
    docx_document_reader.cpp
    plain_text_document_reader.cpp
    docx_document_writer.cpp
    pdf_document_writer.cpp
    text_document_writer.cpp
    odt_document_reader.cpp
    pdf_document_reader.cpp
    odt_document_writer.cpp
)

target_include_directories(mondoc_adapters_formats PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(mondoc_adapters_formats PUBLIC cxx_std_20)

target_link_libraries(mondoc_adapters_formats
    PUBLIC mondoc_domain libzip::zip pugixml::pugixml stduuid
    PRIVATE $<IF:$<TARGET_EXISTS:podofo_shared>,podofo_shared,podofo_static>)

# Formats is a pure C++ adapter and must not link any Qt component.
