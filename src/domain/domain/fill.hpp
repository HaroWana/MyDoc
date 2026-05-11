#pragma once
#include <string>
#include <vector>
#include "mondoc/id.hpp"
#include "confidence.hpp"
#include "source_ref.hpp"

namespace mondoc::domain {

struct Fill {
    FieldId field_id_;
    std::string current_value_;
    Confidence confidence_ = Confidence::Manual;
    std::vector<SourceRef> source_refs_;
};

}  // namespace mondoc::domain
