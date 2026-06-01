#include "mondoc_bundle_reader.hpp"

#include <nlohmann/json.hpp>
#include <uuid.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr zip_uint64_t kMaxBundleBytes = 50ULL * 1024 * 1024;  // 50 MB DoS guard

std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string generateUuid() {
    static thread_local std::mt19937 generator{[] {
        std::random_device rd;
        std::array<std::seed_seq::result_type, std::mt19937::state_size> seed{};
        std::generate(seed.begin(), seed.end(), std::ref(rd));
        std::seed_seq seq(seed.begin(), seed.end());
        return std::mt19937{seq};
    }()};
    uuids::uuid_random_generator gen{generator};
    return uuids::to_string(gen());
}

bool isValidEntryName(const std::string& name) {
    if (name.empty()) return false;
    if (name.find('/')  != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name.find("..") != std::string::npos) return false;
    return true;
}

mondoc::domain::FieldType stringToFieldType(std::string_view s) {
    using mondoc::domain::FieldType;
    if (s == "paragraph") return FieldType::Paragraph;
    if (s == "number")    return FieldType::Number;
    if (s == "date")      return FieldType::Date;
    if (s == "checkbox")  return FieldType::Checkbox;
    if (s == "dropdown")  return FieldType::Dropdown;
    return FieldType::Text;
}

mondoc::domain::FieldOrigin stringToFieldOrigin(std::string_view s) {
    using mondoc::domain::FieldOrigin;
    if (s == "form_control") return FieldOrigin::FormControl;
    if (s == "placeholder")  return FieldOrigin::Placeholder;
    return FieldOrigin::Unknown;
}

std::optional<mondoc::domain::FieldLocation>
locationFromJson(const nlohmann::json& j) {
    if (j.is_null() || !j.is_object()) return std::nullopt;
    if (!j.contains("type") || !j.at("type").is_string()) return std::nullopt;
    const std::string type = j.at("type").get<std::string>();

    mondoc::domain::FieldLocation loc{};
    if (type == "pdf") {
        mondoc::domain::PdfLocation pdf{};
        pdf.page_index = j.value("page_index", 0);
        pdf.x = j.value("x", 0.0);
        pdf.y = j.value("y", 0.0);
        pdf.w = j.value("w", 0.0);
        pdf.h = j.value("h", 0.0);
        loc.pdf = pdf;
        return loc;
    }
    if (type == "text") {
        mondoc::domain::TextLocation tx{};
        tx.paragraph_index = j.value("paragraph_index", 0);
        tx.char_offset = j.value("char_offset", 0);
        loc.text = tx;
        return loc;
    }
    return std::nullopt;
}

mondoc::expected<std::string, mondoc::Error>
readManifestEntry(zip_t* zf) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(zf, "manifest.json", 0, &st) < 0) {
        return mondoc::unexpected(mondoc::Error::generic(
            "manifest.json not found"));
    }
    if ((st.valid & ZIP_STAT_SIZE) && st.size > kMaxBundleBytes) {
        return mondoc::unexpected(mondoc::Error::generic(
            "bundle too large (ZIP bomb guard)"));
    }

    zip_file_t* entry = zip_fopen(zf, "manifest.json", 0);
    if (!entry) {
        return mondoc::unexpected(mondoc::Error::generic(
            "failed to open manifest.json"));
    }

    std::string buf;
    if (st.valid & ZIP_STAT_SIZE) {
        buf.resize(static_cast<std::size_t>(st.size));
        zip_int64_t got = zip_fread(entry, buf.data(), st.size);
        if (got < 0) {
            zip_fclose(entry);
            return mondoc::unexpected(mondoc::Error::generic(
                "read error in manifest.json"));
        }
        buf.resize(static_cast<std::size_t>(got));
    } else {
        std::array<char, 64 * 1024> chunk{};
        for (;;) {
            zip_int64_t got = zip_fread(entry, chunk.data(), chunk.size());
            if (got < 0) {
                zip_fclose(entry);
                return mondoc::unexpected(mondoc::Error::generic(
                    "read error in manifest.json"));
            }
            if (got == 0) break;
            if (buf.size() + static_cast<std::size_t>(got) > kMaxBundleBytes) {
                zip_fclose(entry);
                return mondoc::unexpected(mondoc::Error::generic(
                    "bundle too large (ZIP bomb guard)"));
            }
            buf.append(chunk.data(), static_cast<std::size_t>(got));
        }
    }

    zip_fclose(entry);
    return buf;
}

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
MondocBundleReader::read(const std::filesystem::path& src) {
    int errCode = 0;
    const std::string nativePath = pathToUtf8(src);
    zip_t* zf = zip_open(nativePath.c_str(), ZIP_RDONLY, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    auto manifestRaw = readManifestEntry(zf);
    if (!manifestRaw) {
        zip_discard(zf);
        return mondoc::unexpected(manifestRaw.error());
    }
    zip_discard(zf);

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(*manifestRaw);
    } catch (const nlohmann::json::parse_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"manifest parse error: "} + e.what()));
    }

    if (!doc.contains("mondoc_version") || !doc.at("mondoc_version").is_number_integer()
        || doc.at("mondoc_version").get<int>() != 1) {
        return mondoc::unexpected(mondoc::Error::generic(
            "unsupported mondoc_version"));
    }

    std::string sourceFilename;
    try {
        if (doc.contains("source_filename")) {
            sourceFilename = doc.at("source_filename").get<std::string>();
        }
    } catch (const nlohmann::json::type_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"manifest wrong type: "} + e.what()));
    }
    if (!isValidEntryName(sourceFilename)) {
        return mondoc::unexpected(mondoc::Error::generic(
            "invalid entry name in manifest"));
    }

    mondoc::domain::Template t;
    t.id_ = mondoc::TemplateId{generateUuid()};
    t.source_path_ = sourceFilename;
    try {
        if (doc.contains("name"))          t.name_          = doc.at("name").get<std::string>();
        if (doc.contains("source_format")) t.source_format_ = doc.at("source_format").get<std::string>();
        if (doc.contains("fields") && doc.at("fields").is_array()) {
            for (const auto& jf : doc.at("fields")) {
                mondoc::domain::Field f;
                f.id_   = mondoc::FieldId{generateUuid()};
                f.name_ = jf.value("name", std::string{});
                f.type_   = stringToFieldType(jf.value("type", std::string{"text"}));
                f.origin_ = stringToFieldOrigin(jf.value("origin", std::string{"unknown"}));
                if (jf.contains("location")) {
                    f.location_ = locationFromJson(jf.at("location"));
                }
                t.fields_.push_back(std::move(f));
            }
        }
    } catch (const nlohmann::json::type_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"manifest wrong type: "} + e.what()));
    }

    return t;
}

}  // namespace mondoc::adapters::formats
