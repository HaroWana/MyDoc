#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "mondoc/id.hpp"

namespace mondoc::domain {

struct AiSourceDoc {
    mondoc::SourceDocId id_;
    std::string title_;
    std::string text_;
};

struct AiExtractedFact {
    std::size_t source_index_ = 0;
    std::int64_t char_start_  = 0;
    std::int64_t char_end_    = 0;
    std::string excerpt_;
    std::string summary_;
};

}  // namespace mondoc::domain
