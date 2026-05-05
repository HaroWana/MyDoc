#pragma once
#include <cstdint>
#include <string>
#include "mondoc/id.hpp"

namespace mondoc::domain {

struct TextRange {
    std::int64_t begin_ = 0;
    std::int64_t end_   = 0;
};

struct SourceRef {
    SourceDocId source_id_;
    TextRange range_;
    std::string excerpt_;
};

}  // namespace mondoc::domain
