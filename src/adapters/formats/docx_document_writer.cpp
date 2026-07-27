#include "docx_document_writer.hpp"

#include "detail/placeholders.hpp"
#include "detail/zip_util.hpp"
#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uint64_t kMaxDocxBytes = 50ULL * 1024 * 1024;

using detail::normalize;

mondoc::expected<std::string, mondoc::Error>
readDocumentXml(zip_t* zf) {
    return detail::readZipEntry(zf, "word/document.xml", kMaxDocxBytes);
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

void applySdtFills(pugi::xml_node node,
                   const std::unordered_map<std::string, std::string>& byName) {
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:sdt") {
            pugi::xml_node props = child.child("w:sdtPr");
            std::string raw;
            if (props) {
                if (auto alias = props.child("w:alias")) {
                    raw = alias.attribute("w:val").value();
                }
                if (raw.empty()) {
                    if (auto tag = props.child("w:tag")) {
                        raw = tag.attribute("w:val").value();
                    }
                }
            }
            const std::string name = normalize(raw);
            auto it = byName.find(name);
            if (!name.empty() && it != byName.end()) {
                pugi::xml_node content = child.child("w:sdtContent");
                if (!content) content = child.append_child("w:sdtContent");
                while (content.first_child()) {
                    content.remove_child(content.first_child());
                }
                pugi::xml_node run = content.append_child("w:r");
                pugi::xml_node t   = run.append_child("w:t");
                t.append_attribute("xml:space") = "preserve";
                t.append_child(pugi::node_pcdata).set_value(it->second.c_str());
            }
        }
        applySdtFills(child, byName);
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

bool hasAnyPlaceholder(const std::string& s) {
    static const std::regex kAny{
        R"(\{\{[^}]+\}\}|\[[A-Za-z_][A-Za-z0-9_ ]+\]|<[A-Za-z_][A-Za-z0-9_ ]+>)"};
    return std::regex_search(s, kAny);
}

void applyPlaceholderFills(pugi::xml_node node,
                           const std::unordered_map<std::string, std::string>& byName) {
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:p") {
            const std::string original = reconstructParagraphText(child);
            if (hasAnyPlaceholder(original)) {
                const std::string mutated = detail::substituteAll(original, byName);
                if (mutated != original) {
                    pugi::xml_node firstRun = child.child("w:r");
                    pugi::xml_node firstRPr;
                    if (firstRun) firstRPr = firstRun.child("w:rPr");

                    pugi::xml_node r = child.child("w:r");
                    while (r) {
                        pugi::xml_node next = r.next_sibling("w:r");
                        child.remove_child(r);
                        r = next;
                    }
                    pugi::xml_node newRun = child.append_child("w:r");
                    if (firstRPr) newRun.append_copy(firstRPr);
                    pugi::xml_node t = newRun.append_child("w:t");
                    t.append_attribute("xml:space") = "preserve";
                    t.append_child(pugi::node_pcdata).set_value(mutated.c_str());
                }
            }
        }
        applyPlaceholderFills(child, byName);
    }
}

}  // namespace

mondoc::expected<void, mondoc::Error>
DocxDocumentWriter::write(const mondoc::domain::Template& tpl,
                          const std::vector<mondoc::domain::Fill>& fills,
                          const std::filesystem::path& dest) {
    std::error_code ec;
    if (tpl.source_path_.empty()) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "template source_path_ is empty"));
    }
    std::filesystem::copy_file(tpl.source_path_, dest,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"copy template: "} + ec.message()));
    }

    int errCode = 0;
    const std::string nativePath = pathToUtf8(dest);
    zip_t* zf = zip_open(nativePath.c_str(), 0, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    auto xmlResult = readDocumentXml(zf);
    if (!xmlResult) {
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(xmlResult.error());
    }

    pugi::xml_document doc;
    auto parse = doc.load_buffer(xmlResult->data(), xmlResult->size());
    if (parse.status != pugi::status_ok) {
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(parse.description()));
    }

    const auto byName = fillsByName(tpl, fills);
    applySdtFills(doc, byName);
    applyPlaceholderFills(doc, byName);

    std::stringstream ss;
    doc.save(ss, "", pugi::format_raw, pugi::encoding_utf8);
    std::string newXml = ss.str();

    zip_int64_t idx = zip_name_locate(zf, "word/document.xml", ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "word/document.xml entry missing"));
    }
    zip_source_t* src = zip_source_buffer(zf, newXml.data(), newXml.size(), 0);
    if (!src) {
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic("zip_source_buffer failed"));
    }
    if (zip_file_replace(zf, static_cast<zip_uint64_t>(idx), src,
                         ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(src);
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic("zip_file_replace failed"));
    }

    if (zip_close(zf) < 0) {
        zip_discard(zf);
        std::filesystem::remove(dest, ec);
        return mondoc::unexpected(mondoc::Error::generic("zip_close failed"));
    }
    return {};
}

}  // namespace mondoc::adapters::formats
