#include "format_registry.hpp"

#include "mondoc/util.hpp"

#include "docx_document_reader.hpp"
#include "docx_document_writer.hpp"
#include "odt_document_reader.hpp"
#include "odt_document_writer.hpp"
#include "pdf_document_reader.hpp"
#include "pdf_document_writer.hpp"
#include "plain_text_document_reader.hpp"
#include "text_document_writer.hpp"

namespace mondoc::adapters::formats {

std::unique_ptr<mondoc::domain::IDocumentReader>
readerForPath(const std::filesystem::path& path) {
    const std::string ext = mondoc::lowercaseExtension(path);
    if (ext == ".docx") return std::make_unique<DocxDocumentReader>();
    if (ext == ".odt")  return std::make_unique<OdtDocumentReader>();
    if (ext == ".pdf")  return std::make_unique<PdfDocumentReader>();
    if (ext == ".txt" || ext == ".md")
        return std::make_unique<PlainTextDocumentReader>();
    return nullptr;
}

std::unique_ptr<mondoc::domain::IDocumentWriter>
writerForExtension(std::string_view ext) {
    if (ext == ".docx") return std::make_unique<DocxDocumentWriter>();
    if (ext == ".odt")  return std::make_unique<OdtDocumentWriter>();
    if (ext == ".pdf")  return std::make_unique<PdfDocumentWriter>();
    if (ext == ".txt")  return std::make_unique<TextDocumentWriter>();
    return nullptr;
}

}  // namespace mondoc::adapters::formats
