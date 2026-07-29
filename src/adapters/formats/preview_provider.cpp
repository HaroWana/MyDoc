#include "preview_provider.hpp"

#include "mondoc/util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace mondoc::adapters::formats {

namespace {
constexpr int kConvertTimeoutSeconds = 60;

bool isConvertible(std::string_view ext) {
    return ext == ".docx" || ext == ".odt" || ext == ".txt" || ext == ".md";
}

int runCommand(const std::string& cmd) {
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return -1;
    std::array<char, 256> buf{};
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        // discard output
    }
#if defined(_WIN32)
    int status = _pclose(pipe);
    return status;
#else
    int status = pclose(pipe);
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::string quoted(const std::filesystem::path& p) {
    return "\"" + mondoc::pathToUtf8(p) + "\"";
}

std::string quoted(const std::string& s) {
    return "\"" + s + "\"";
}

bool containsQuote(const std::filesystem::path& p) {
    return mondoc::pathToUtf8(p).find('"') != std::string::npos;
}

// Template ids are UUIDs in practice; this allowlist also rules out shell
// metacharacters (the id is interpolated into a popen'd command) and path
// traversal (the id becomes a path segment under cacheDir).
bool isValidTemplateId(const std::string& id) {
    if (id.empty() || id.find("..") != std::string::npos) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-';
    });
}

}  // namespace

std::filesystem::path findLibreOffice(const std::filesystem::path& override) {
    std::error_code ec;
    if (!override.empty() && std::filesystem::is_regular_file(override, ec)) {
        return override;
    }

    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return {};

    std::string_view pathView{pathEnv};
    size_t start = 0;
    while (start <= pathView.size()) {
        size_t sep = pathView.find(':', start);
        std::string_view dir = (sep == std::string_view::npos)
                                    ? pathView.substr(start)
                                    : pathView.substr(start, sep - start);
        if (!dir.empty()) {
            std::filesystem::path dirPath{dir};
            for (const char* name : {"soffice", "libreoffice"}) {
                std::filesystem::path candidate = dirPath / name;
                if (std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }
        }
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return {};
}

mondoc::expected<PreviewResult, mondoc::Error>
previewPdfFor(const std::filesystem::path& source,
              const std::string& templateId,
              const std::filesystem::path& cacheDir,
              const std::filesystem::path& sofficePath) {
    const std::string ext = mondoc::lowercaseExtension(source);
    if (ext == ".pdf") {
        return PreviewResult{source, false};
    }
    if (!isConvertible(ext)) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "unsupported preview source extension: " + ext));
    }

    if (containsQuote(source) || containsQuote(cacheDir) || containsQuote(sofficePath)) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "path must not contain a double quote"));
    }
    if (!isValidTemplateId(templateId)) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "templateId must be non-empty and contain only letters, digits, '.', '_', '-'"));
    }

    const std::filesystem::path pdfPath = cacheDir / (templateId + ".pdf");
    const std::filesystem::path sidecarPath = cacheDir / (templateId + ".json");

    std::error_code ec;
    const auto sourceSize = std::filesystem::file_size(source, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat source: "} + ec.message()));
    }
    const auto sourceMtime =
        std::filesystem::last_write_time(source, ec).time_since_epoch().count();
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat source: "} + ec.message()));
    }

    if (std::filesystem::exists(pdfPath, ec) && std::filesystem::exists(sidecarPath, ec)) {
        std::ifstream sidecarIn(sidecarPath, std::ios::binary);
        nlohmann::json sidecar;
        bool valid = false;
        if (sidecarIn) {
            try {
                sidecarIn >> sidecar;
                valid = sidecar.value("size", std::uintmax_t{0}) == sourceSize &&
                        sidecar.value("mtime", std::int64_t{0}) == sourceMtime;
            } catch (const nlohmann::json::exception&) {
                valid = false;
            }
        }
        if (valid) {
            return PreviewResult{pdfPath, false};
        }
    }

    if (sofficePath.empty() || !std::filesystem::is_regular_file(sofficePath, ec)) {
        return mondoc::unexpected(mondoc::Error::generic(
            "LibreOffice not found — install it or set its path in Settings"));
    }

    std::filesystem::path input = source;
    if (ext == ".md") {
        input = std::filesystem::temp_directory_path() / (templateId + "-preview.txt");
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(input, std::ios::binary);
        out << in.rdbuf();
    }

    const std::filesystem::path profileDir = cacheDir / ("lo-profile-" + templateId);

    std::string cmd;
#if !defined(_WIN32)
    cmd += "timeout " + std::to_string(kConvertTimeoutSeconds) + " ";
#endif
    cmd += quoted(sofficePath) +
           " --headless --norestore " +
           quoted(std::string{"-env:UserInstallation=file://"} + mondoc::pathToUtf8(profileDir)) +
           " --convert-to pdf --outdir " + quoted(cacheDir) + " " + quoted(input) +
           " >/dev/null 2>&1";

    const int exitCode = runCommand(cmd);

    const std::filesystem::path convertedPdf = cacheDir / (input.stem().string() + ".pdf");
    if (exitCode != 0 || !std::filesystem::exists(convertedPdf, ec)) {
        std::filesystem::remove_all(profileDir, ec);
        return mondoc::unexpected(mondoc::Error::generic(
            "LibreOffice conversion failed (exit code " + std::to_string(exitCode) + ")"));
    }

    if (convertedPdf != pdfPath) {
        std::filesystem::rename(convertedPdf, pdfPath, ec);
        if (ec) {
            std::filesystem::remove_all(profileDir, ec);
            return mondoc::unexpected(mondoc::Error::generic(
                std::string{"cannot rename converted pdf: "} + ec.message()));
        }
    }

    nlohmann::json sidecar{{"size", sourceSize}, {"mtime", sourceMtime}};
    std::ofstream sidecarOut(sidecarPath, std::ios::binary);
    sidecarOut << sidecar.dump();
    sidecarOut.close();

    std::filesystem::remove_all(profileDir, ec);

    return PreviewResult{pdfPath, true};
}

}  // namespace mondoc::adapters::formats
