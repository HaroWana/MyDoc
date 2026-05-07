#include "docx_document_reader.hpp"

#include <pugixml.hpp>
#include <uuid.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <random>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr zip_uint64_t kMaxDocxBytes = 50ULL * 1024 * 1024;  // 50 MB DoS guard
constexpr std::size_t  kReadChunkSize = 64 * 1024;

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

bool extensionIsDocx(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (ext.size() != 5) return false;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".docx";
}

std::string normalize(std::string_view raw) {
    std::string s{raw};
    auto first = s.find_first_not_of(" \t\r\n");
    auto last  = s.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    s = s.substr(first, last - first + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

mondoc::domain::FieldType inferFieldType(const pugi::xml_node& sdtPr) {
    if (sdtPr.child("w:date"))         return mondoc::domain::FieldType::Date;
    if (sdtPr.child("w:dropDownList")) return mondoc::domain::FieldType::Dropdown;
    if (sdtPr.child("w:comboBox"))     return mondoc::domain::FieldType::Dropdown;
    if (sdtPr.child("w:checkbox"))     return mondoc::domain::FieldType::Checkbox;
    if (sdtPr.child("w:richText"))     return mondoc::domain::FieldType::Paragraph;
    if (sdtPr.child("w:num"))          return mondoc::domain::FieldType::Number;
    return mondoc::domain::FieldType::Text;
}

mondoc::expected<std::string, mondoc::Error>
readDocumentXml(zip_t* zf) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(zf, "word/document.xml", 0, &st) < 0) {
        return mondoc::unexpected(
            mondoc::Error::generic("word/document.xml not found"));
    }

    zip_file_t* entry = zip_fopen(zf, "word/document.xml", 0);
    if (!entry) {
        return mondoc::unexpected(
            mondoc::Error::generic("failed to open word/document.xml"));
    }

    std::string xml;
    if ((st.valid & ZIP_STAT_SIZE) && st.size <= kMaxDocxBytes) {
        xml.resize(static_cast<std::size_t>(st.size));
        zip_int64_t got = zip_fread(entry, xml.data(), st.size);
        if (got < 0) {
            zip_fclose(entry);
            return mondoc::unexpected(
                mondoc::Error::generic("read error in word/document.xml"));
        }
        xml.resize(static_cast<std::size_t>(got));
    } else {
        std::array<char, kReadChunkSize> buf{};
        for (;;) {
            zip_int64_t got = zip_fread(entry, buf.data(), buf.size());
            if (got < 0) {
                zip_fclose(entry);
                return mondoc::unexpected(
                    mondoc::Error::generic("read error in word/document.xml"));
            }
            if (got == 0) break;
            if (xml.size() + static_cast<std::size_t>(got) > kMaxDocxBytes) {
                zip_fclose(entry);
                return mondoc::unexpected(
                    mondoc::Error::generic("word/document.xml exceeds size limit"));
            }
            xml.append(buf.data(), static_cast<std::size_t>(got));
        }
    }

    zip_fclose(entry);
    return xml;
}

void extractSdtFields(const pugi::xml_node& root,
                      std::vector<mondoc::domain::Field>& out,
                      std::unordered_set<std::string>& seen) {
    for (pugi::xml_node node : root.children()) {
        if (std::string_view{node.name()} == "w:sdt") {
            pugi::xml_node props = node.child("w:sdtPr");
            if (props) {
                std::string raw;
                if (auto alias = props.child("w:alias")) {
                    raw = alias.attribute("w:val").value();
                }
                if (raw.empty()) {
                    if (auto tag = props.child("w:tag")) {
                        raw = tag.attribute("w:val").value();
                    }
                }
                std::string name = normalize(raw);
                if (!name.empty() && seen.insert(name).second) {
                    mondoc::domain::Field f;
                    f.id_   = mondoc::FieldId{generateUuid()};
                    f.name_ = std::move(name);
                    f.type_ = inferFieldType(props);
                    out.push_back(std::move(f));
                }
            }
        }
        extractSdtFields(node, out, seen);
    }
}

std::string reconstructParagraphText(const pugi::xml_node& para) {
    std::string text;
    for (pugi::xml_node run : para.children("w:r")) {
        for (pugi::xml_node t : run.children("w:t")) {
            text += t.child_value();
        }
    }
    return text;
}

void scanPlaceholders(const std::string& text,
                      std::vector<mondoc::domain::Field>& out,
                      std::unordered_set<std::string>& seen) {
    static const std::regex kDoubleBrace{R"(\{\{\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*\}\})"};
    static const std::regex kSquareBracket{R"(\[([A-Za-z_][A-Za-z0-9_ ]+?)\])"};
    static const std::regex kAngleBracket{R"(<([A-Za-z_][A-Za-z0-9_ ]+?)>)"};

    auto runOne = [&](const std::regex& re) {
        for (auto it = std::sregex_iterator{text.begin(), text.end(), re};
             it != std::sregex_iterator{}; ++it) {
            std::string name = normalize((*it)[1].str());
            if (name.empty() || !seen.insert(name).second) continue;
            mondoc::domain::Field f;
            f.id_   = mondoc::FieldId{generateUuid()};
            f.name_ = std::move(name);
            f.type_ = mondoc::domain::FieldType::Text;
            out.push_back(std::move(f));
        }
    };
    runOne(kDoubleBrace);
    runOne(kSquareBracket);
    runOne(kAngleBracket);
}

void extractPlaceholderFields(const pugi::xml_node& root,
                              std::vector<mondoc::domain::Field>& out,
                              std::unordered_set<std::string>& seen) {
    for (pugi::xml_node node : root.children()) {
        if (std::string_view{node.name()} == "w:p") {
            scanPlaceholders(reconstructParagraphText(node), out, seen);
        }
        extractPlaceholderFields(node, out, seen);
    }
}

std::vector<mondoc::domain::Field>
unionFields(std::vector<mondoc::domain::Field> sdtFields,
            std::vector<mondoc::domain::Field> placeholderFields) {
    std::vector<mondoc::domain::Field> result;
    std::unordered_set<std::string> seen;

    for (auto& f : sdtFields) {
        if (seen.insert(f.name_).second) {
            result.push_back(std::move(f));
        }
    }
    for (auto& f : placeholderFields) {
        if (seen.insert(f.name_).second) {
            result.push_back(std::move(f));
        }
    }
    return result;
}

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
DocxDocumentReader::read(const std::filesystem::path& path) {
    if (!extensionIsDocx(path)) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "expected .docx extension"));
    }

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat file: "} + ec.message()));
    }
    if (fileSize > kMaxDocxBytes) {
        return mondoc::unexpected(mondoc::Error::generic("file too large"));
    }

    int errCode = 0;
    const std::string nativePath = pathToUtf8(path);
    zip_t* zf = zip_open(nativePath.c_str(), ZIP_RDONLY, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    auto xmlResult = readDocumentXml(zf);
    if (!xmlResult) {
        zip_discard(zf);
        return mondoc::unexpected(xmlResult.error());
    }
    zip_discard(zf);

    pugi::xml_document doc;
    auto parseResult = doc.load_buffer(xmlResult->data(), xmlResult->size());
    if (parseResult.status != pugi::status_ok) {
        return mondoc::unexpected(mondoc::Error::generic(parseResult.description()));
    }

    std::vector<mondoc::domain::Field> sdtFields;
    std::unordered_set<std::string> sdtSeen;
    extractSdtFields(doc, sdtFields, sdtSeen);

    std::vector<mondoc::domain::Field> placeholderFields;
    std::unordered_set<std::string> placeholderSeen;
    extractPlaceholderFields(doc, placeholderFields, placeholderSeen);

    mondoc::domain::Template t;
    t.id_            = mondoc::TemplateId{generateUuid()};
    t.name_          = path.stem().string();
    t.source_format_ = "docx";
    t.fields_        = unionFields(std::move(sdtFields), std::move(placeholderFields));
    t.source_path_   = std::filesystem::absolute(path, ec);
    if (ec) {
        t.source_path_ = path;
    }
    return t;
}

}  // namespace mondoc::adapters::formats
