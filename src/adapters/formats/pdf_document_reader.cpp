#include "pdf_document_reader.hpp"

#include "detail/placeholders.hpp"
#include "mondoc/util.hpp"

#include <podofo/podofo.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

using detail::normalize;

constexpr std::uint64_t kMaxPdfBytes = 50ULL * 1024 * 1024;

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
PdfDocumentReader::read(const std::filesystem::path& path) {
    if (!mondoc::hasExtension(path, ".pdf")) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "PdfDocumentReader: expected .pdf, got " + path.extension().string()));
    }

    std::error_code sizeEc;
    auto fileSize = std::filesystem::file_size(path, sizeEc);
    if (sizeEc) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat file: "} + sizeEc.message()));
    }
    if (fileSize > kMaxPdfBytes) {
        return mondoc::unexpected(mondoc::Error::generic("file too large"));
    }

    try {
        PoDoFo::PdfMemDocument document;
        document.Load(pathToUtf8(path));

        PoDoFo::PdfAcroForm* acroForm = document.GetAcroForm();

        if (acroForm == nullptr || acroForm->GetFieldCount() == 0) {
            if (acroForm != nullptr && acroForm->GetDictionary().HasKey("XFA")) {
                return mondoc::unexpected(mondoc::Error::generic(
                    "XFA-only PDF forms are not supported. "
                    "Export from Adobe Reader as a standard PDF to use MonDoc."));
            }
            std::error_code ec;
            mondoc::domain::Template t;
            t.id_            = mondoc::TemplateId{generateUuid()};
            t.name_          = pathToUtf8(path.stem());
            t.source_format_ = "pdf";
            t.source_path_   = std::filesystem::absolute(path, ec);
            if (ec) t.source_path_ = path;
            return t;
        }

        std::vector<mondoc::domain::Field> fields;
        std::unordered_set<std::string> seen;

        for (PoDoFo::PdfField* field : *acroForm) {
            mondoc::domain::FieldType type;
            switch (field->GetType()) {
                case PoDoFo::PdfFieldType::TextBox:
                    type = mondoc::domain::FieldType::Text; break;
                case PoDoFo::PdfFieldType::CheckBox:
                case PoDoFo::PdfFieldType::RadioButton:
                    type = mondoc::domain::FieldType::Checkbox; break;
                case PoDoFo::PdfFieldType::ComboBox:
                case PoDoFo::PdfFieldType::ListBox:
                    type = mondoc::domain::FieldType::Dropdown; break;
                case PoDoFo::PdfFieldType::PushButton:
                case PoDoFo::PdfFieldType::Signature:
                case PoDoFo::PdfFieldType::Unknown:
                default:
                    continue;
            }

            std::string name = normalize(field->GetFullName());
            if (name.empty() || !seen.insert(name).second) continue;

            mondoc::domain::Field f;
            f.id_     = mondoc::FieldId{generateUuid()};
            f.name_   = name;
            f.type_   = type;
            f.origin_ = mondoc::domain::FieldOrigin::FormControl;
            fields.push_back(std::move(f));
        }

        std::error_code ec;
        mondoc::domain::Template t;
        t.id_            = mondoc::TemplateId{generateUuid()};
        t.name_          = pathToUtf8(path.stem());
        t.source_format_ = "pdf";
        t.fields_        = std::move(fields);
        t.source_path_   = std::filesystem::absolute(path, ec);
        if (ec) t.source_path_ = path;
        if (acroForm->GetDictionary().HasKey("XFA")) {
            t.warnings_.push_back(
                "This PDF contains a hybrid XFA form. MonDoc fills its standard "
                "(AcroForm) fields, but viewers that render the XFA layer, such "
                "as Adobe Acrobat, may not display the filled values.");
        }
        return t;

    } catch (const PoDoFo::PdfError& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    } catch (const std::exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    }
}

}  // namespace mondoc::adapters::formats
