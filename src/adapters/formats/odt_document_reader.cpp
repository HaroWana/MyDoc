#include "odt_document_reader.hpp"

#include "detail/odt_text.hpp"
#include "detail/placeholders.hpp"
#include "detail/zip_util.hpp"
#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uint64_t kMaxOdtBytes = 50ULL * 1024 * 1024;

using detail::normalize;

mondoc::expected<std::string, mondoc::Error>
readContentXml(zip_t* zf) {
    return detail::readZipEntry(zf, "content.xml", kMaxOdtBytes);
}

void extractFormControls(const pugi::xml_document& doc,
                         std::vector<mondoc::domain::Field>& fields,
                         std::unordered_set<std::string>& seen) {
    pugi::xml_node body = doc.first_child()
                             .child("office:body")
                             .child("office:text")
                             .child("office:forms");

    std::function<void(pugi::xml_node)> traverseForm;
    traverseForm = [&](pugi::xml_node formNode) {
        for (pugi::xml_node child : formNode.children()) {
            std::string_view childName{child.name()};
            if (childName == "form:form") {
                traverseForm(child);
                continue;
            }
            mondoc::domain::FieldType type;
            bool skip = false;
            if      (childName == "form:text")     type = mondoc::domain::FieldType::Text;
            else if (childName == "form:textarea")  type = mondoc::domain::FieldType::Paragraph;
            else if (childName == "form:checkbox")  type = mondoc::domain::FieldType::Checkbox;
            else if (childName == "form:listbox")   type = mondoc::domain::FieldType::Dropdown;
            else if (childName == "form:combobox")  type = mondoc::domain::FieldType::Dropdown;
            else if (childName == "form:date")      type = mondoc::domain::FieldType::Date;
            else if (childName == "form:number")    type = mondoc::domain::FieldType::Number;
            else skip = true;

            if (skip) continue;

            std::string rawName = child.attribute("form:name").value();
            if (rawName.empty()) {
                rawName = "field_" + std::to_string(fields.size());
            }
            std::string name = normalize(rawName);
            if (name.empty() || !seen.insert(name).second) continue;

            mondoc::domain::Field f;
            f.id_     = mondoc::FieldId{generateUuid()};
            f.name_   = name;
            f.type_   = type;
            f.origin_ = mondoc::domain::FieldOrigin::FormControl;
            fields.push_back(std::move(f));
        }
    };
    for (pugi::xml_node formNode : body.children("form:form")) {
        traverseForm(formNode);
    }
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

void collectTextParagraphs(const pugi::xml_node& node, std::string& out) {
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "text:p") {
            detail::appendOdtText(child, out);
            out += ' ';
        } else {
            collectTextParagraphs(child, out);
        }
    }
}

void extractOdtPlaceholders(const pugi::xml_document& doc,
                             std::vector<mondoc::domain::Field>& fields,
                             std::unordered_set<std::string>& seen) {
    std::string text;
    collectTextParagraphs(doc, text);
    std::vector<mondoc::domain::Field> tmp;
    std::unordered_set<std::string> tmpSeen;
    scanPlaceholders(text, tmp, tmpSeen);
    for (auto& f : tmp) {
        f.origin_ = mondoc::domain::FieldOrigin::Placeholder;
        if (seen.insert(f.name_).second) {
            fields.push_back(std::move(f));
        }
    }
}

std::vector<mondoc::domain::Field>
unionFields(std::vector<mondoc::domain::Field> primary,
            std::vector<mondoc::domain::Field> secondary) {
    std::vector<mondoc::domain::Field> result;
    std::unordered_set<std::string> seen;

    for (auto& f : primary) {
        if (seen.insert(f.name_).second) {
            result.push_back(std::move(f));
        }
    }
    for (auto& f : secondary) {
        if (seen.insert(f.name_).second) {
            result.push_back(std::move(f));
        }
    }
    return result;
}

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
OdtDocumentReader::read(const std::filesystem::path& path) {
    if (!mondoc::hasExtension(path, ".odt")) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "OdtDocumentReader: expected .odt, got " + path.extension().string()));
    }

    std::error_code sizeEc;
    auto sz = std::filesystem::file_size(path, sizeEc);
    if (!sizeEc && sz > kMaxOdtBytes) {
        return mondoc::unexpected(mondoc::Error::generic("ODT file too large (> 50 MB)"));
    }

    int errCode = 0;
    zip_t* zf = zip_open(pathToUtf8(path).c_str(), ZIP_RDONLY, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    auto xmlResult = readContentXml(zf);
    zip_discard(zf);
    if (!xmlResult) return mondoc::unexpected(xmlResult.error());

    pugi::xml_document doc;
    auto pr = doc.load_buffer(xmlResult->data(), xmlResult->size());
    if (pr.status != pugi::status_ok) {
        return mondoc::unexpected(mondoc::Error::generic(pr.description()));
    }

    std::vector<mondoc::domain::Field> formFields;
    std::unordered_set<std::string> formSeen;
    extractFormControls(doc, formFields, formSeen);

    std::vector<mondoc::domain::Field> placeholderFields;
    std::unordered_set<std::string> placeholderSeen = formSeen;
    extractOdtPlaceholders(doc, placeholderFields, placeholderSeen);

    std::error_code ec;
    mondoc::domain::Template t;
    t.id_            = mondoc::TemplateId{generateUuid()};
    t.name_          = path.stem().string();
    t.source_format_ = "odt";
    t.fields_        = unionFields(std::move(formFields), std::move(placeholderFields));
    t.source_path_   = std::filesystem::absolute(path, ec);
    if (ec) t.source_path_ = path;
    return t;
}

}  // namespace mondoc::adapters::formats
