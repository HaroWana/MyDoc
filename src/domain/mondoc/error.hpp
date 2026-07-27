#pragma once
#include <string>
#include <utility>

namespace mondoc {

class Error {
public:
    enum class Kind {
        Generic, StorageOpen, Migration, NotFound, InvalidArgument,
        Cancelled, Unreachable, RateLimited, BadResponse, Conflict
    };

    Error(Kind k, std::string msg) : kind_(k), message_(std::move(msg)) {}

    static Error generic(std::string m)         { return {Kind::Generic, std::move(m)}; }
    static Error storageOpen(std::string m)     { return {Kind::StorageOpen, std::move(m)}; }
    static Error migration(std::string m)       { return {Kind::Migration, std::move(m)}; }
    static Error notFound(std::string m)        { return {Kind::NotFound, std::move(m)}; }
    static Error invalidArgument(std::string m) { return {Kind::InvalidArgument, std::move(m)}; }
    static Error cancelled(std::string m)       { return {Kind::Cancelled, std::move(m)}; }
    static Error unreachable(std::string m)     { return {Kind::Unreachable, std::move(m)}; }
    static Error rateLimited(std::string m)     { return {Kind::RateLimited, std::move(m)}; }
    static Error badResponse(std::string m)     { return {Kind::BadResponse, std::move(m)}; }
    static Error conflict(std::string m)        { return {Kind::Conflict, std::move(m)}; }

    Kind kind() const noexcept { return kind_; }
    const std::string& message() const noexcept { return message_; }

private:
    Kind kind_;
    std::string message_;
};

}  // namespace mondoc
