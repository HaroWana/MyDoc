#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace mondoc::tests_support {

// Builds a collision-safe path under the system temp dir:
// <prefix><random>_<steady_clock nanos><ext>. Removes any pre-existing file
// at that path defensively (should never actually collide).
inline std::filesystem::path uniqueTempPath(const std::string& prefix,
                                            const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / (prefix + suffix + ext);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

inline void writeFile(const std::filesystem::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f << body;
}

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    return std::string{std::istreambuf_iterator<char>{in},
                       std::istreambuf_iterator<char>{}};
}

}  // namespace mondoc::tests_support
