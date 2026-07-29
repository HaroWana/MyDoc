#include "preview_provider.hpp"

#include "mondoc/util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <array>
#include <cstdio>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace mondoc::adapters::formats {

namespace {
constexpr int kConvertTimeoutSeconds = 60;

bool isConvertible(std::string_view ext) {
    return ext == ".docx" || ext == ".odt" || ext == ".txt" || ext == ".md";
}

#if defined(_WIN32)
// cmd.exe expands %, ^, & etc. even inside double quotes; reject rather
// than attempt escaping (Linux-first — the POSIX path spawns without a
// shell and needs no such guard).
bool containsCmdMetachar(const std::string& s) {
    return s.find_first_of("\"%^&|<>!") != std::string::npos;
}

std::string quoted(const std::filesystem::path& p) {
    return "\"" + mondoc::pathToUtf8(p) + "\"";
}

std::string quoted(const std::string& s) {
    return "\"" + s + "\"";
}

int runCommand(const std::string& cmd) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    std::array<char, 256> buf{};
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        // discard output
    }
    return _pclose(pipe);
}
#else
// No shell involved: argv is handed straight to the child, so filenames with
// shell metacharacters (backticks, `$`, quotes, ...) can't be interpreted.
int runSoffice(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = 0;
    const int spawnRc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawnRc != 0) return -1;

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

// Template ids are UUIDs in practice; this allowlist also rules out
// path traversal (the id becomes a path segment under cacheDir).
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

#if defined(_WIN32)
    // Still shells out via _popen/cmd.exe on Windows (Linux-first; the POSIX
    // path below uses posix_spawnp with an argv vector, no shell) — so every
    // string entering the command line must be free of cmd metacharacters.
    for (const std::filesystem::path& p : {input, cacheDir, sofficePath, profileDir}) {
        if (containsCmdMetachar(mondoc::pathToUtf8(p))) {
            return mondoc::unexpected(mondoc::Error::invalidArgument(
                "path contains characters unsafe for the Windows shell: " +
                mondoc::pathToUtf8(p)));
        }
    }
    const std::string cmd =
        quoted(sofficePath) + " --headless --norestore " +
        quoted(std::string{"-env:UserInstallation=file://"} + mondoc::pathToUtf8(profileDir)) +
        " --convert-to pdf --outdir " + quoted(cacheDir) + " " + quoted(input);
    const int exitCode = runCommand(cmd);
#else
    const std::vector<std::string> args = {
        "timeout", std::to_string(kConvertTimeoutSeconds),
        mondoc::pathToUtf8(sofficePath),
        "--headless", "--norestore",
        "-env:UserInstallation=file://" + mondoc::pathToUtf8(profileDir),
        "--convert-to", "pdf",
        "--outdir", mondoc::pathToUtf8(cacheDir),
        mondoc::pathToUtf8(input),
    };
    const int exitCode = runSoffice(args);
#endif

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
