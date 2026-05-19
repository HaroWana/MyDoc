#include "pdf_document_reader.hpp"

#include <podofo/podofo.h>
#include <uuid.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

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

bool extensionIsPdf(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (ext.size() != 4) return false;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".pdf";
}

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
PdfDocumentReader::read(const std::filesystem::path& path) {
    if (!extensionIsPdf(path)) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "PdfDocumentReader: expected .pdf, got " + path.extension().string()));
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
            t.name_          = path.stem().string();
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
            f.id_   = mondoc::FieldId{generateUuid()};
            f.name_ = name;
            f.type_ = type;
            fields.push_back(std::move(f));
        }

        std::error_code ec;
        mondoc::domain::Template t;
        t.id_            = mondoc::TemplateId{generateUuid()};
        t.name_          = path.stem().string();
        t.source_format_ = "pdf";
        t.fields_        = std::move(fields);
        t.source_path_   = std::filesystem::absolute(path, ec);
        if (ec) t.source_path_ = path;
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
