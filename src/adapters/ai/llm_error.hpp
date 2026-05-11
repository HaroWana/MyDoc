#pragma once
#include <string>
#include <utility>

namespace mondoc::adapters::ai {

class LlmError {
public:
    enum class Kind { Unreachable, RateLimited, BadResponse, Cancelled };

    LlmError(Kind k, std::string m) : kind_(k), message_(std::move(m)) {}

    static LlmError unreachable(std::string m) { return {Kind::Unreachable, std::move(m)}; }
    static LlmError rateLimited()              { return {Kind::RateLimited, "rate limited"}; }
    static LlmError badResponse(std::string m) { return {Kind::BadResponse, std::move(m)}; }
    static LlmError cancelled()                { return {Kind::Cancelled, "cancelled"}; }

    Kind kind() const noexcept { return kind_; }
    const std::string& message() const noexcept { return message_; }

private:
    Kind kind_;
    std::string message_;
};

}  // namespace mondoc::adapters::ai
