#pragma once
#include <compare>
#include <string>
#include <utility>

namespace mondoc {

template <typename Tag>
class Id {
public:
    Id() = default;
    explicit Id(std::string v) : value_(std::move(v)) {}
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const Id&) const = default;

private:
    std::string value_;
};

struct TemplateIdTag {};
struct FieldIdTag {};
struct FillSessionIdTag {};
struct SourceDocIdTag {};

using TemplateId    = Id<TemplateIdTag>;
using FieldId       = Id<FieldIdTag>;
using FillSessionId = Id<FillSessionIdTag>;
using SourceDocId   = Id<SourceDocIdTag>;

}  // namespace mondoc
