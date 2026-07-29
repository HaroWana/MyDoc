#include "pdf_document_reader.hpp"

#include "detail/placeholders.hpp"
#include "mondoc/util.hpp"

#include <podofo/podofo.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
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

        // Build field collection from acroform
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

        // Build index map after all fields are collected to avoid vector reallocation issues
        std::unordered_map<std::string, std::size_t> fieldMap;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            fieldMap[fields[i].name_] = i;
        }

        // Search pages for widgets and populate field locations
        for (unsigned pageIdx = 0; pageIdx < document.GetPages().GetCount(); ++pageIdx) {
            auto& page = document.GetPages().GetPageAt(pageIdx);
            auto& annotations = page.GetAnnotations();
            unsigned annotCount = annotations.GetCount();

            for (unsigned i = 0; i < annotCount; ++i) {
                auto& annot = annotations.GetAnnotAt(i);
                if (auto* widget = dynamic_cast<PoDoFo::PdfAnnotationWidget*>(&annot)) {
                    try {
                        auto& widgetField = widget->GetField();
                        std::string widgetFieldName = normalize(widgetField.GetFullName());
                        if (widgetFieldName.empty()) continue;

                        auto it = fieldMap.find(widgetFieldName);
                        if (it != fieldMap.end()) {
                            std::size_t fieldIdx = it->second;
                            mondoc::domain::Field& f = fields[fieldIdx];
                            const auto pageRect = page.GetRect();
                            const auto r = widget->GetRect();
                            // First widget in page/annotation order wins for multi-widget fields
                            if (pageRect.Width > 0 && pageRect.Height > 0 && !f.location_.has_value()) {
                                mondoc::domain::PdfLocation loc;
                                loc.page_index = static_cast<int>(pageIdx);
                                loc.x = (r.X - pageRect.X) / pageRect.Width;
                                loc.w = r.Width / pageRect.Width;
                                loc.h = r.Height / pageRect.Height;
                                // PDF origin is bottom-left; PdfLocation is top-left based.
                                loc.y = 1.0 - ((r.Y - pageRect.Y) + r.Height) / pageRect.Height;
                                f.location_ = mondoc::domain::FieldLocation{loc, std::nullopt};
                            }
                        }
                    } catch (const PoDoFo::PdfError&) {
                        // Widget might not have an associated field, skip it
                    }
                }
            }
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
                "This PDF contains a hybrid XFA form. MonDoc reads only its "
                "standard (AcroForm) field definitions; the XFA layer is "
                "ignored, and exports are generated as a new document rather "
                "than a filled copy of this form.");
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
