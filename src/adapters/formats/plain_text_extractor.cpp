#include "plain_text_extractor.hpp"

#include "detail/odt_text.hpp"
#include "detail/zip_util.hpp"
#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <podofo/podofo.h>

#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uint64_t kMaxSourceBytes = 50ULL * 1024 * 1024;  // 50 MB
constexpr int kMaxXmlDepth = 256;

mondoc::expected<std::string, mondoc::Error>
readEntryFromArchive(const std::filesystem::path& path, const char* entryName) {
    int errCode = 0;
    zip_t* zf = zip_open(pathToUtf8(path).c_str(), ZIP_RDONLY, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }
    auto data = detail::readZipEntry(zf, entryName, kMaxSourceBytes);
    zip_discard(zf);
    return data;
}

void collectParagraphText(const pugi::xml_node& node, std::string& para, int depth = 0) {
    if (depth > kMaxXmlDepth) return;
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:t") {
            para += child.child_value();
        }
        collectParagraphText(child, para, depth + 1);
    }
}

void collectWtTextRecursive(const pugi::xml_node& node,
                            std::string& out,
                            bool& firstParagraph,
                            int depth = 0) {
    if (depth > kMaxXmlDepth) return;
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:p") {
            if (!firstParagraph) out += '\n';
            firstParagraph = false;
            collectParagraphText(child, out);
        } else {
            collectWtTextRecursive(child, out, firstParagraph, depth + 1);
        }
    }
}

void collectOdtParagraphs(const pugi::xml_node& node, std::string& out, int depth = 0) {
    if (depth > kMaxXmlDepth) return;
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "text:p") {
            detail::appendOdtText(child, out);
            out += '\n';
        } else {
            collectOdtParagraphs(child, out, depth + 1);
        }
    }
}

mondoc::expected<std::string, mondoc::Error>
extractDocxText(const std::filesystem::path& path) {
    auto xml = readEntryFromArchive(path, "word/document.xml");
    if (!xml) return mondoc::unexpected(xml.error());

    pugi::xml_document doc;
    auto pr = doc.load_buffer(xml->data(), xml->size());
    if (pr.status != pugi::status_ok) {
        return mondoc::unexpected(mondoc::Error::generic(pr.description()));
    }

    std::string out;
    bool firstParagraph = true;
    collectWtTextRecursive(doc, out, firstParagraph);
    return out;
}

mondoc::expected<std::string, mondoc::Error>
extractOdtText(const std::filesystem::path& path) {
    auto xml = readEntryFromArchive(path, "content.xml");
    if (!xml) return mondoc::unexpected(xml.error());

    pugi::xml_document doc;
    auto pr = doc.load_buffer(xml->data(), xml->size());
    if (pr.status != pugi::status_ok) {
        return mondoc::unexpected(mondoc::Error::generic(pr.description()));
    }

    std::string out;
    collectOdtParagraphs(doc, out);
    return out;
}

mondoc::expected<std::string, mondoc::Error>
extractPdfText(const std::filesystem::path& path) {
    try {
        PoDoFo::PdfMemDocument document;
        document.Load(pathToUtf8(path));
        std::string text;
        unsigned count = document.GetPages().GetCount();
        for (unsigned i = 0; i < count; i++) {
            auto& page = document.GetPages().GetPageAt(i);
            std::vector<PoDoFo::PdfTextEntry> entries;
            page.ExtractTextTo(entries);
            for (auto& entry : entries) {
                text += entry.Text;
                text += ' ';
            }
            text += '\n';
        }
        return text;
    } catch (const PoDoFo::PdfError& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    } catch (const std::exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    }
}

mondoc::expected<std::string, mondoc::Error>
extractFileText(const std::filesystem::path& path) {
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat file: "} + ec.message()));
    }
    if (fileSize > kMaxSourceBytes) {
        return mondoc::unexpected(mondoc::Error::generic("file too large"));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return mondoc::unexpected(mondoc::Error::generic("failed to open file"));
    }
    return std::string{std::istreambuf_iterator<char>{in},
                       std::istreambuf_iterator<char>{}};
}

}  // namespace

mondoc::expected<std::string, mondoc::Error>
extractPlainText(const std::filesystem::path& src) {
    const std::string ext = lowercaseExtension(src);
    if (ext == ".odt")  return extractOdtText(src);
    if (ext == ".pdf")  return extractPdfText(src);
    if (ext == ".docx") return extractDocxText(src);
    if (ext == ".txt" || ext == ".md") return extractFileText(src);
    return mondoc::unexpected(mondoc::Error::invalidArgument(
        std::string{"unsupported source format: "} + ext));
}

}  // namespace mondoc::adapters::formats
