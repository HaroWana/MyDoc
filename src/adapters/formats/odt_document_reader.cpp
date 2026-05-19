#include "odt_document_reader.hpp"

namespace mondoc::adapters::formats {

mondoc::expected<mondoc::domain::Template, mondoc::Error>
OdtDocumentReader::read(const std::filesystem::path&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

}  // namespace mondoc::adapters::formats
