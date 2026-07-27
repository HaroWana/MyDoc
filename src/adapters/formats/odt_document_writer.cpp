#include "odt_document_writer.hpp"

#include "detail/odt_text.hpp"
#include "detail/placeholders.hpp"
#include "detail/zip_util.hpp"
#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
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

std::unordered_map<std::string, std::string>
fillsByName(const mondoc::domain::Template& tpl,
            const std::vector<mondoc::domain::Fill>& fills) {
    std::unordered_map<std::string, const mondoc::domain::Field*> idToField;
    for (const auto& f : tpl.fields_) {
        idToField.emplace(f.id_.value(), &f);
    }
    std::unordered_map<std::string, std::string> out;
    for (const auto& fill : fills) {
        auto it = idToField.find(fill.field_id_.value());
        if (it != idToField.end()) {
            out[normalize(it->second->name_)] = fill.current_value_;
        }
    }
    return out;
}

bool isTruthyFill(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value == "true" || value == "1" || value == "yes" || value == "checked";
}

void applyFormControlFills(pugi::xml_document& doc,
                           const std::unordered_map<std::string, std::string>& byName,
                           const std::vector<mondoc::domain::Field>& fields) {
    std::unordered_set<std::string> formControlNames;
    for (const auto& f : fields) {
        if (f.origin_ == mondoc::domain::FieldOrigin::FormControl) {
            formControlNames.insert(normalize(f.name_));
        }
    }

    pugi::xml_node formsNode =
        doc.first_child()
           .child("office:body")
           .child("office:text")
           .child("office:forms");

    std::function<void(pugi::xml_node)> processFormNode;
    processFormNode = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            std::string_view n{child.name()};
            if (n == "form:form") {
                processFormNode(child);
                continue;
            }
            if (n != "form:text"     && n != "form:textarea" &&
                n != "form:checkbox" && n != "form:listbox"  &&
                n != "form:combobox" && n != "form:date"     &&
                n != "form:number") {
                continue;
            }
            std::string fieldName = normalize(child.attribute("form:name").value());
            if (fieldName.empty()) continue;
            if (formControlNames.find(fieldName) == formControlNames.end()) continue;

            auto it = byName.find(fieldName);
            if (it == byName.end()) continue;

            if (n == "form:checkbox") {
                pugi::xml_attribute valueAttr = child.attribute("form:current-value");
                if (valueAttr) child.remove_attribute(valueAttr);

                pugi::xml_attribute stateAttr = child.attribute("form:current-state");
                if (!stateAttr) stateAttr = child.append_attribute("form:current-state");
                stateAttr.set_value(isTruthyFill(it->second) ? "checked" : "unchecked");
                continue;
            }

            pugi::xml_attribute attr = child.attribute("form:current-value");
            if (attr) {
                attr.set_value(it->second.c_str());
            } else {
                child.append_attribute("form:current-value") = it->second.c_str();
            }
        }
    };
    for (pugi::xml_node formNode : formsNode.children("form:form")) {
        processFormNode(formNode);
    }
}

void applyPlaceholderFills(pugi::xml_document& doc,
                           const std::unordered_map<std::string, std::string>& byName) {
    std::function<void(pugi::xml_node)> traverseText;
    traverseText = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            if (std::string_view{child.name()} == "text:p") {
                std::string original;
                detail::appendOdtText(child, original);
                std::string mutated = detail::substituteAll(original, byName);
                if (mutated != original) {
                    while (child.first_child()) {
                        child.remove_child(child.first_child());
                    }
                    child.append_child(pugi::node_pcdata).set_value(mutated.c_str());
                }
                continue;
            }
            traverseText(child);
        }
    };
    traverseText(doc);
}

}  // namespace

mondoc::expected<void, mondoc::Error>
OdtDocumentWriter::write(const mondoc::domain::Template& tpl,
                         const std::vector<mondoc::domain::Fill>& fills,
                         const std::filesystem::path& dest) {
    std::error_code cpEc;
    std::filesystem::copy_file(tpl.source_path_, dest,
                               std::filesystem::copy_options::overwrite_existing, cpEc);
    if (cpEc) {
        return mondoc::unexpected(mondoc::Error::generic(
            "copy ODT template: " + cpEc.message()));
    }

    int errCode = 0;
    zip_t* zf = zip_open(pathToUtf8(dest).c_str(), 0, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    auto xmlResult = readContentXml(zf);
    if (!xmlResult) {
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(xmlResult.error());
    }

    pugi::xml_document doc;
    auto pr = doc.load_buffer(xmlResult->data(), xmlResult->size());
    if (pr.status != pugi::status_ok) {
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic(pr.description()));
    }

    auto byName = fillsByName(tpl, fills);

    applyFormControlFills(doc, byName, tpl.fields_);
    applyPlaceholderFills(doc, byName);

    std::ostringstream ss;
    doc.save(ss, "", pugi::format_raw, pugi::encoding_utf8);
    const std::string xmlOut = ss.str();

    zip_int64_t idx = zip_name_locate(zf, "content.xml", ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic(
            "content.xml not found in ODT ZIP for replace"));
    }

    auto* buf = zip_source_buffer(zf, xmlOut.data(), xmlOut.size(), 0);
    if (!buf) {
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic("zip_source_buffer failed"));
    }
    if (zip_file_replace(zf, static_cast<zip_uint64_t>(idx), buf, ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(buf);
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic("zip_file_replace failed"));
    }
    if (zip_close(zf) < 0) {
        zip_discard(zf);
        std::filesystem::remove(dest, cpEc);
        return mondoc::unexpected(mondoc::Error::generic("zip_close failed"));
    }
    return {};
}

}  // namespace mondoc::adapters::formats
