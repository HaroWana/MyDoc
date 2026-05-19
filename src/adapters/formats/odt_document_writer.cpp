#include "odt_document_writer.hpp"

namespace mondoc::adapters::formats {

mondoc::expected<void, mondoc::Error>
OdtDocumentWriter::write(const mondoc::domain::Template&,
                         const std::vector<mondoc::domain::Fill>&,
                         const std::filesystem::path&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

}  // namespace mondoc::adapters::formats
