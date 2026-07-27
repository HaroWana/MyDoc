#pragma once

#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

#include <zip.h>

#include <cstdint>
#include <string>

namespace mondoc::adapters::formats::detail {

// Reads the ZIP entry `name` from `za` fully into memory, enforcing `maxBytes`
// before allocating. Loops zip_fread() until the full entry has been read;
// a negative return or a short read at EOF is reported as an error.
mondoc::expected<std::string, mondoc::Error>
readZipEntry(zip_t* za, const char* name, std::uint64_t maxBytes);

}  // namespace mondoc::adapters::formats::detail
