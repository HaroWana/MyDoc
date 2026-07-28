#include "llm_client.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
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

bool isAcceptableHost(const std::string& url) {
    if (startsWith(url, "https://")) return true;
    if (!startsWith(url, "http://")) return false;

    std::string_view rest(url);
    rest.remove_prefix(7);  // strlen("http://")

    std::string_view hostPart;
    if (!rest.empty() && rest.front() == '[') {
        auto closeBracket = rest.find(']');
        if (closeBracket == std::string_view::npos) return false;
        hostPart = rest.substr(0, closeBracket + 1);
    } else {
        auto end = rest.find_first_of("/:");
        hostPart = (end == std::string_view::npos) ? rest : rest.substr(0, end);
    }

    return hostPart == "localhost" || hostPart == "127.0.0.1" || hostPart == "[::1]";
}

LlmError LlmClient::classifyHttpStatus(int status, std::string bodyTrunc) {
    if (status == 429) {
        return LlmError::rateLimited();
    }
    if (status >= 500) {
        return LlmError::badResponse(
            "HTTP " + std::to_string(status) + ": " + std::move(bodyTrunc));
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

mondoc::expected<std::unique_ptr<LlmClient>, LlmError>
LlmClient::create(std::string host, std::string apiKey, std::string pathPrefix) {
    if (!isAcceptableHost(host)) {
        return mondoc::unexpected(LlmError::unreachable("insecure LLM URL: " + host));
    }
    return std::unique_ptr<LlmClient>(
        new LlmClient(std::move(host), std::move(apiKey), std::move(pathPrefix)));
}

LlmClient::LlmClient(std::string host, std::string apiKey, std::string pathPrefix)
    : host_(std::move(host)),
      api_key_(std::move(apiKey)),
      path_prefix_(std::move(pathPrefix)) {}

mondoc::expected<std::string, LlmError>
LlmClient::chat(const std::string& body, const std::atomic<bool>* cancelled) {
    httplib::Client cli(host_);
    cli.set_connection_timeout(std::chrono::seconds(10));
    cli.set_read_timeout(std::chrono::seconds(60));
    cli.set_write_timeout(std::chrono::seconds(10));

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key_},
        {"Content-Type",  "application/json"}
    };

    std::string responseBody;
    bool tooLarge = false;
    bool wasCancelled = false;
    auto receiver = [&](const char* data, std::size_t len) {
        if (cancelled != nullptr && cancelled->load()) {
            wasCancelled = true;
            return false;
        }
        responseBody.append(data, len);
        if (responseBody.size() > kMaxResponseBytes) {
            tooLarge = true;
            return false;
        }
        return true;
    };

    auto res = cli.Post(path_prefix_ + "/chat/completions",
                        headers, body, "application/json", receiver);

    if (wasCancelled) {
        return mondoc::unexpected(LlmError::cancelled());
    }
    if (tooLarge) {
        return mondoc::unexpected(LlmError::badResponse("response too large"));
    }
    if (!res) {
        return mondoc::unexpected(
            LlmError::unreachable(httplib::to_string(res.error())));
    }
    if (res->status >= 200 && res->status < 300) {
        return responseBody;
    }
    std::string trunc = responseBody.substr(
        0, std::min<std::size_t>(256, responseBody.size()));
    return mondoc::unexpected(
        classifyHttpStatus(res->status, std::move(trunc)));
}

}  // namespace mondoc::adapters::ai
