#pragma once
#include <string>
#include <utility>

namespace mondoc {

class Error {
public:
    enum class Kind { Generic, StorageOpen, Migration, NotFound, InvalidArgument };

    Error(Kind k, std::string msg) : kind_(k), message_(std::move(msg)) {}

    static Error generic(std::string m)         { return {Kind::Generic, std::move(m)}; }
    static Error storageOpen(std::string m)     { return {Kind::StorageOpen, std::move(m)}; }
    static Error migration(std::string m)       { return {Kind::Migration, std::move(m)}; }
    static Error notFound(std::string m)        { return {Kind::NotFound, std::move(m)}; }
    static Error invalidArgument(std::string m) { return {Kind::InvalidArgument, std::move(m)}; }

    Kind kind() const noexcept { return kind_; }
    const std::string& message() const noexcept { return message_; }

private:
    Kind kind_;
    std::string message_;
};

}  // namespace mondoc
