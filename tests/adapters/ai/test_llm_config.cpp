#include <catch2/catch_test_macros.hpp>

#include "llm_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace mondoc::adapters::ai;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path()
                / ("mondoc_test_llm_config_" + suffix + ext);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

void writeFile(const std::filesystem::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f << body;
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

TEST_CASE("LlmConfig::loadFromJson: missing file returns unconfigured (FILL-12)",
          "[adapters.ai][llm_config]") {
    auto path = uniqueTempPath(".json");
    auto result = LlmConfig::loadFromJson(path);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->isConfigured());
    REQUIRE(result->api_url.empty());
    REQUIRE(result->api_key.empty());
    REQUIRE(result->model.empty());
}

TEST_CASE("LlmConfig::loadFromJson: empty file returns unconfigured (FILL-12)",
          "[adapters.ai][llm_config]") {
    TempFile tmp{uniqueTempPath(".json")};
    writeFile(tmp.path, "");

    auto result = LlmConfig::loadFromJson(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->isConfigured());
}

TEST_CASE("LlmConfig::loadFromJson: well-formed config round-trips",
          "[adapters.ai][llm_config]") {
    TempFile tmp{uniqueTempPath(".json")};
    writeFile(tmp.path,
              R"({"api_url":"https://hub.example.com/v1",)"
              R"("api_key":"sk-test",)"
              R"("model":"gpt-4o"})");

    auto result = LlmConfig::loadFromJson(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->isConfigured());
    REQUIRE(result->api_url == "https://hub.example.com/v1");
    REQUIRE(result->api_key == "sk-test");
    REQUIRE(result->model == "gpt-4o");
}

TEST_CASE("LlmConfig::loadFromJson: malformed JSON returns Error",
          "[adapters.ai][llm_config]") {
    TempFile tmp{uniqueTempPath(".json")};
    writeFile(tmp.path, "not json {");

    auto result = LlmConfig::loadFromJson(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
    const auto& msg = result.error().message();
    const bool mentionsParseOrJson =
        msg.find("parse") != std::string::npos ||
        msg.find("json")  != std::string::npos;
    REQUIRE(mentionsParseOrJson);
}

TEST_CASE("LlmConfig::loadFromJson: empty object returns unconfigured (partial = disabled)",
          "[adapters.ai][llm_config]") {
    TempFile tmp{uniqueTempPath(".json")};
    writeFile(tmp.path, "{}");

    auto result = LlmConfig::loadFromJson(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->isConfigured());
    REQUIRE(result->api_url.empty());
    REQUIRE(result->api_key.empty());
    REQUIRE(result->model.empty());
}

TEST_CASE("LlmConfig::loadFromJson: wrong type for api_url returns Error",
          "[adapters.ai][llm_config]") {
    TempFile tmp{uniqueTempPath(".json")};
    writeFile(tmp.path, R"({"api_url": 42})");

    auto result = LlmConfig::loadFromJson(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}

TEST_CASE("LlmConfig::saveToJson: round-trips all three fields [phase05][adapters.ai.llm_config]") {
    auto path = uniqueTempPath(".json");
    TempFile tmp{path};

    LlmConfig cfg;
    cfg.api_url = "https://hub.example.com/v1";
    cfg.api_key = "sk-secret";
    cfg.model   = "gpt-4o";

    auto saveResult = cfg.saveToJson(path);
    REQUIRE(saveResult.has_value());

    auto loadResult = LlmConfig::loadFromJson(path);
    REQUIRE(loadResult.has_value());
    REQUIRE(loadResult->api_url == "https://hub.example.com/v1");
    REQUIRE(loadResult->api_key == "sk-secret");
    REQUIRE(loadResult->model == "gpt-4o");
    REQUIRE(loadResult->isConfigured());
}

#if !defined(_WIN32)
TEST_CASE("LlmConfig::saveToJson: creates key file with 0600 permissions from the start (SAI-8)",
          "[adapters.ai][llm_config]") {
    auto path = uniqueTempPath(".json");
    TempFile tmp{path};

    LlmConfig cfg;
    cfg.api_url = "https://hub.example.com/v1";
    cfg.api_key = "sk-secret";
    cfg.model   = "gpt-4o";

    auto saveResult = cfg.saveToJson(path);
    REQUIRE(saveResult.has_value());

    std::error_code ec;
    auto status = std::filesystem::status(path, ec);
    REQUIRE_FALSE(ec);
    auto perms = status.permissions() & std::filesystem::perms::mask;
    REQUIRE(perms == (std::filesystem::perms::owner_read |
                      std::filesystem::perms::owner_write));
}
#endif

#if !defined(_WIN32)
TEST_CASE("LlmConfig::loadFromJson: distinguishes fs error from absent file (SAI-9)",
          "[adapters.ai][llm_config]") {
    if (::geteuid() == 0) {
        SKIP("running as root: directory permissions are not enforced");
    }

    auto parentDir = std::filesystem::temp_directory_path() /
        ("mondoc_test_llm_config_noaccess_" +
         std::to_string(std::mt19937_64{std::random_device{}()}()));
    std::filesystem::create_directory(parentDir);
    auto childPath = parentDir / "config.json";

    std::filesystem::permissions(parentDir, std::filesystem::perms::none);

    auto result = LlmConfig::loadFromJson(childPath);

    std::filesystem::permissions(parentDir, std::filesystem::perms::owner_all);
    std::filesystem::remove_all(parentDir);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}
#endif

#if !defined(_WIN32)
TEST_CASE("LlmConfig::saveToJson: unwritable path returns Error [phase05][adapters.ai.llm_config]") {
    if (::geteuid() == 0) {
        SKIP("running as root: directory permissions are not enforced");
    }

    auto parentDir = std::filesystem::temp_directory_path() /
        ("mondoc_test_llm_config_nowrite_" +
         std::to_string(std::mt19937_64{std::random_device{}()}()));
    std::filesystem::create_directory(parentDir);
    auto badPath = parentDir / "config.json";

    std::filesystem::permissions(parentDir, std::filesystem::perms::none);

    LlmConfig cfg;
    cfg.api_url = "https://x.com";
    cfg.api_key = "k";
    cfg.model   = "m";
    auto result = cfg.saveToJson(badPath);

    std::filesystem::permissions(parentDir, std::filesystem::perms::owner_all);
    std::filesystem::remove_all(parentDir);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}
#endif
