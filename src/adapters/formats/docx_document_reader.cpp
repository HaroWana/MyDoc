#include "docx_document_reader.hpp"

#include "detail/placeholders.hpp"
#include "detail/zip_util.hpp"
#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uint64_t kMaxDocxBytes = 50ULL * 1024 * 1024;  // 50 MB DoS guard
constexpr int kMaxXmlDepth = 256;

using detail::normalize;

mondoc::domain::FieldType inferFieldType(const pugi::xml_node& sdtPr) {
    if (sdtPr.child("w:date"))         return mondoc::domain::FieldType::Date;
    if (sdtPr.child("w:dropDownList")) return mondoc::domain::FieldType::Dropdown;
    if (sdtPr.child("w:comboBox"))     return mondoc::domain::FieldType::Dropdown;
    if (sdtPr.child("w14:checkbox"))   return mondoc::domain::FieldType::Checkbox;
    if (sdtPr.child("w:richText"))     return mondoc::domain::FieldType::Paragraph;
    if (sdtPr.child("w:num"))          return mondoc::domain::FieldType::Number;
    return mondoc::domain::FieldType::Text;
}

mondoc::expected<std::string, mondoc::Error>
readDocumentXml(zip_t* zf) {
    return detail::readZipEntry(zf, "word/document.xml", kMaxDocxBytes);
}

void extractSdtFields(const pugi::xml_node& root,
                      std::vector<mondoc::domain::Field>& out,
                      std::unordered_set<std::string>& seen,
                      int depth = 0) {
    if (depth > kMaxXmlDepth) return;
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
        extractSdtFields(node, out, seen, depth + 1);
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
    for (auto& name : detail::scanPlaceholders(text)) {
        if (!seen.insert(name).second) continue;
        mondoc::domain::Field f;
        f.id_   = mondoc::FieldId{generateUuid()};
        f.name_ = std::move(name);
        f.type_ = mondoc::domain::FieldType::Text;
        out.push_back(std::move(f));
    }
}

void extractPlaceholderFields(const pugi::xml_node& root,
                              std::vector<mondoc::domain::Field>& out,
                              std::unordered_set<std::string>& seen,
                              int depth = 0) {
    if (depth > kMaxXmlDepth) return;
    for (pugi::xml_node node : root.children()) {
        if (std::string_view{node.name()} == "w:p") {
            scanPlaceholders(reconstructParagraphText(node), out, seen);
        }
        extractPlaceholderFields(node, out, seen, depth + 1);
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
    if (!mondoc::hasExtension(path, ".docx")) {
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
    t.name_          = pathToUtf8(path.stem());
    t.source_format_ = "docx";
    t.fields_        = unionFields(std::move(sdtFields), std::move(placeholderFields));
    t.source_path_   = std::filesystem::absolute(path, ec);
    if (ec) {
        t.source_path_ = path;
    }
    return t;
}

}  // namespace mondoc::adapters::formats
