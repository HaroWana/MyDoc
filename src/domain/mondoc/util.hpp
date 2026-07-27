#pragma once

#include <uuid.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

namespace mondoc {

inline std::string generateUuid() {
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

// path.u8string() returns UTF-8 unconditionally; path.string() uses the OS
// native ANSI codepage on Windows and mangles non-ASCII characters
// (RESEARCH.md Risk R3 / PITFALLS C4).
inline std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

inline std::string lowercaseExtension(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

inline bool hasExtension(const std::filesystem::path& p, std::string_view dotExt) {
    return lowercaseExtension(p) == dotExt;
}

inline std::int64_t unixNowSeconds() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace mondoc
