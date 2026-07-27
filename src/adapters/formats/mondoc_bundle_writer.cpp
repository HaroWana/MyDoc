#include "mondoc_bundle_writer.hpp"

#include "mondoc/util.hpp"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uint64_t kMaxSourceBytes = 50ULL * 1024 * 1024;

std::string isoTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%FT%TZ");
    return ss.str();
}

std::string fieldTypeToStr(mondoc::domain::FieldType t) {
    using mondoc::domain::FieldType;
    switch (t) {
        case FieldType::Text:      return "text";
        case FieldType::Paragraph: return "paragraph";
        case FieldType::Number:    return "number";
        case FieldType::Date:      return "date";
        case FieldType::Checkbox:  return "checkbox";
        case FieldType::Dropdown:  return "dropdown";
    }
    return "text";
}

std::string fieldOriginToStr(mondoc::domain::FieldOrigin o) {
    using mondoc::domain::FieldOrigin;
    switch (o) {
        case FieldOrigin::FormControl: return "form_control";
        case FieldOrigin::Placeholder: return "placeholder";
        case FieldOrigin::Unknown:     return "unknown";
    }
    return "unknown";
}

nlohmann::json locationToJson(const std::optional<mondoc::domain::FieldLocation>& loc) {
    if (!loc.has_value()) return nlohmann::json(nullptr);
    if (loc->pdf.has_value()) {
        const auto& p = *loc->pdf;
        return nlohmann::json{
            {"type", "pdf"},
            {"page_index", p.page_index},
            {"x", p.x},
            {"y", p.y},
            {"w", p.w},
            {"h", p.h},
        };
    }
    if (loc->text.has_value()) {
        const auto& tx = *loc->text;
        return nlohmann::json{
            {"type", "text"},
            {"paragraph_index", tx.paragraph_index},
            {"char_offset", tx.char_offset},
        };
    }
    return nlohmann::json(nullptr);
}

std::string buildManifest(const mondoc::domain::Template& tpl,
                          const std::string& sourceFilename) {
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& f : tpl.fields_) {
        fields.push_back({
            {"id", f.id_.value()},
            {"name", f.name_},
            {"type", fieldTypeToStr(f.type_)},
            {"origin", fieldOriginToStr(f.origin_)},
            {"location", locationToJson(f.location_)},
        });
    }
    nlohmann::json doc = {
        {"mondoc_version", 1},
        {"name", tpl.name_},
        {"source_format", tpl.source_format_},
        {"source_filename", sourceFilename},
        {"exported_at", isoTimestamp()},
        {"fields", fields},
    };
    return doc.dump(2);
}

mondoc::expected<std::vector<char>, mondoc::Error>
readSourceFile(const std::filesystem::path& path) {
    std::error_code sizeEc;
    auto sz = std::filesystem::file_size(path, sizeEc);
    if (sizeEc) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat source document: "} + sizeEc.message()));
    }
    if (sz > kMaxSourceBytes) {
        return mondoc::unexpected(mondoc::Error::generic(
            "source document too large (> 50 MB)"));
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return mondoc::unexpected(mondoc::Error::generic(
            "cannot open source document"));
    }
    try {
        std::vector<char> buf{std::istreambuf_iterator<char>{in},
                              std::istreambuf_iterator<char>{}};
        return buf;
    } catch (const std::bad_alloc&) {
        return mondoc::unexpected(mondoc::Error::generic(
            "source document too large to read into memory"));
    }
}

}  // namespace

mondoc::expected<void, mondoc::Error>
MondocBundleWriter::write(const mondoc::domain::Template& tpl,
                          const std::filesystem::path& dest) {
    std::error_code ec;
    if (tpl.source_path_.empty()) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "template source_path_ is empty"));
    }

    const std::string sourceFilename =
        pathToUtf8(tpl.source_path_.filename());
    if (sourceFilename == "manifest.json") {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "source file cannot be named manifest.json"));
    }

    auto sourceBytes = readSourceFile(tpl.source_path_);
    if (!sourceBytes) {
        return mondoc::unexpected(sourceBytes.error());
    }

    const std::string manifest = buildManifest(tpl, sourceFilename);

    int errCode = 0;
    const std::string nativePath = pathToUtf8(dest);
    zip_t* za = zip_open(nativePath.c_str(),
                         ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!za) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    zip_source_t* manifestSrc =
        zip_source_buffer(za, manifest.data(), manifest.size(), 0);
    if (!manifestSrc) {
        zip_discard(za);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "zip_source_buffer failed (manifest)"));
    }
    if (zip_file_add(za, "manifest.json", manifestSrc,
                     ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(manifestSrc);
        zip_discard(za);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "zip_file_add failed (manifest)"));
    }

    zip_source_t* sourceSrc =
        zip_source_buffer(za, sourceBytes->data(), sourceBytes->size(), 0);
    if (!sourceSrc) {
        zip_discard(za);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "zip_source_buffer failed (source)"));
    }
    if (zip_file_add(za, sourceFilename.c_str(), sourceSrc,
                     ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(sourceSrc);
        zip_discard(za);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "zip_file_add failed (source)"));
    }

    if (zip_close(za) < 0) {
        std::string msg = zip_strerror(za);
        zip_discard(za);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "zip_close failed: " + msg));
    }
    return {};
}

}  // namespace mondoc::adapters::formats
