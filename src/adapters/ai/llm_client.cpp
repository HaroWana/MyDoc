#include "llm_client.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace mondoc::adapters::ai {

namespace {

constexpr std::size_t kMaxResponseBytes = 10ULL * 1024ULL * 1024ULL;

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

bool LlmClient::isAcceptableHost(const std::string& host) {
    if (startsWith(host, "https://")) return true;
    if (startsWith(host, "http://localhost"))    return true;
    if (startsWith(host, "http://127.0.0.1"))    return true;
    if (startsWith(host, "http://[::1]"))        return true;
    return false;
}

LlmError LlmClient::classifyHttpStatus(int status, std::string bodyTrunc) {
    if (status == 429) {
        return LlmError::rateLimited();
    }
    if (status >= 500) {
        return LlmError::badResponse("HTTP " + std::to_string(status));
    }
    if (status >= 400) {
        return LlmError::badResponse(
            "HTTP " + std::to_string(status) + ": " + std::move(bodyTrunc));
    }
    if (status >= 200 && status < 300) {
        return LlmError::badResponse(
            "unexpected 2xx classified as error: " + std::to_string(status));
    }
    return LlmError::badResponse("unexpected status " + std::to_string(status));
}

LlmClient::LlmClient(std::string host, std::string apiKey, std::string pathPrefix)
    : host_(std::move(host)),
      apiKey_(std::move(apiKey)),
      pathPrefix_(std::move(pathPrefix)) {
    if (!isAcceptableHost(host_)) {
        throw std::invalid_argument("non-HTTPS host not allowed: " + host_);
    }
}

mondoc::expected<std::string, LlmError>
LlmClient::chat(const std::string& body) {
    httplib::Client cli(host_);
    cli.set_connection_timeout(std::chrono::seconds(10));
    cli.set_read_timeout(std::chrono::seconds(60));
    cli.set_write_timeout(std::chrono::seconds(10));

    httplib::Headers headers = {
        {"Authorization", "Bearer " + apiKey_},
        {"Content-Type",  "application/json"}
    };

    auto res = cli.Post(pathPrefix_ + "/chat/completions",
                        headers, body, "application/json");
    if (!res) {
        return mondoc::unexpected(
            LlmError::unreachable(httplib::to_string(res.error())));
    }
    if (res->status >= 200 && res->status < 300) {
        if (res->body.size() > kMaxResponseBytes) {
            return mondoc::unexpected(LlmError::badResponse(
                "response body exceeds 10MB cap"));
        }
        return res->body;
    }
    std::string trunc = res->body.substr(
        0, std::min<std::size_t>(256, res->body.size()));
    return mondoc::unexpected(
        classifyHttpStatus(res->status, std::move(trunc)));
}

}  // namespace mondoc::adapters::ai
