#pragma once
#include <cstdint>
#include <vector>
#include "mondoc/id.hpp"
#include "fill.hpp"

namespace mondoc::domain {

enum class FillStatus { Created, Pipelining, Reviewing, Exported, Failed };

struct FillSession {
    FillSessionId id_;
    TemplateId template_id_;
    std::vector<Fill> fills_;
    FillStatus status_ = FillStatus::Created;
    std::int64_t created_at_unix_ = 0;
    std::int64_t updated_at_unix_ = 0;
};

}  // namespace mondoc::domain
