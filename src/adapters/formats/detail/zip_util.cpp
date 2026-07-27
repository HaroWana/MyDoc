#include "detail/zip_util.hpp"

#include <array>

namespace mondoc::adapters::formats::detail {

namespace {
constexpr std::size_t kReadChunkSize = 64 * 1024;
}  // namespace

mondoc::expected<std::string, mondoc::Error>
readZipEntry(zip_t* za, const char* name, std::uint64_t maxBytes) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(za, name, 0, &st) < 0) {
        return mondoc::unexpected(
            mondoc::Error::generic(std::string{name} + " not found"));
    }
    if ((st.valid & ZIP_STAT_SIZE) && st.size > maxBytes) {
        return mondoc::unexpected(
            mondoc::Error::generic(std::string{name} + " exceeds size limit"));
    }

    zip_file_t* entry = zip_fopen(za, name, 0);
    if (!entry) {
        return mondoc::unexpected(
            mondoc::Error::generic(std::string{"failed to open "} + name));
    }

    if (st.valid & ZIP_STAT_SIZE) {
        std::string data(static_cast<std::size_t>(st.size), '\0');
        std::uint64_t total = 0;
        while (total < st.size) {
            zip_int64_t got = zip_fread(entry, data.data() + total,
                                        static_cast<zip_uint64_t>(st.size - total));
            if (got < 0) {
                zip_fclose(entry);
                return mondoc::unexpected(
                    mondoc::Error::generic(std::string{"read error in "} + name));
            }
            if (got == 0) break;
            total += static_cast<std::uint64_t>(got);
        }
        zip_fclose(entry);
        if (total != static_cast<std::uint64_t>(st.size)) {
            return mondoc::unexpected(
                mondoc::Error::generic(std::string{"short read in "} + name));
        }
        return data;
    }

    std::string data;
    std::array<char, kReadChunkSize> buf{};
    for (;;) {
        zip_int64_t got = zip_fread(entry, buf.data(), buf.size());
        if (got < 0) {
            zip_fclose(entry);
            return mondoc::unexpected(
                mondoc::Error::generic(std::string{"read error in "} + name));
        }
        if (got == 0) break;
        if (data.size() + static_cast<std::size_t>(got) > maxBytes) {
            zip_fclose(entry);
            return mondoc::unexpected(
                mondoc::Error::generic(std::string{name} + " exceeds size limit"));
        }
        data.append(buf.data(), static_cast<std::size_t>(got));
    }
    zip_fclose(entry);
    return data;
}

}  // namespace mondoc::adapters::formats::detail
