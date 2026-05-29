#include "mondoc_bundle_reader.hpp"

namespace mondoc::adapters::formats {

mondoc::expected<mondoc::domain::Template, mondoc::Error>
MondocBundleReader::read(const std::filesystem::path& /*src*/) {
    return mondoc::unexpected(mondoc::Error::generic(
        "MondocBundleReader::read not yet implemented"));
}

}  // namespace mondoc::adapters::formats
