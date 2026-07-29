#include <catch2/catch_test_macros.hpp>

#include "llm_client.hpp"

#include <string>
#include <utility>

using mondoc::adapters::ai::LlmClient;
using mondoc::adapters::ai::isAcceptableHost;

TEST_CASE("isAcceptableHost accepts https and exact loopback http",
          "[adapters.ai][llm_client][phase06]") {
    CHECK(isAcceptableHost("https://api.example.com"));
    CHECK(isAcceptableHost("http://localhost"));
    CHECK(isAcceptableHost("http://localhost:8080/v1"));
    CHECK(isAcceptableHost("http://127.0.0.1:11434"));
    CHECK(isAcceptableHost("http://[::1]:8080"));
}

TEST_CASE("isAcceptableHost rejects prefix-spoofed and remote plain http",
          "[adapters.ai][llm_client][phase06]") {
    CHECK_FALSE(isAcceptableHost("http://localhost.evil.com"));
    CHECK_FALSE(isAcceptableHost("http://127.0.0.1.attacker.net"));
    CHECK_FALSE(isAcceptableHost("http://api.example.com"));
    CHECK_FALSE(isAcceptableHost("ftp://localhost"));
}

TEST_CASE("LlmClient::create returns error instead of throwing on bad host",
          "[adapters.ai][llm_client][phase06]") {
    auto r = LlmClient::create("http://api.example.com", "key", "/v1");
    REQUIRE_FALSE(r.has_value());
    auto ok = LlmClient::create("https://api.example.com", "key", "/v1");
    REQUIRE(ok.has_value());
}

TEST_CASE("splitApiUrl: URL path becomes the API prefix",
          "[adapters.ai][llm_client]") {
    using mondoc::adapters::ai::LlmClient;
    CHECK(LlmClient::splitApiUrl("https://ai.example.com/api") ==
          std::pair<std::string, std::string>{"https://ai.example.com", "/api"});
    CHECK(LlmClient::splitApiUrl("https://api.openai.com/v1") ==
          std::pair<std::string, std::string>{"https://api.openai.com", "/v1"});
    CHECK(LlmClient::splitApiUrl("http://localhost:8080/api/v1") ==
          std::pair<std::string, std::string>{"http://localhost:8080", "/api/v1"});
}

TEST_CASE("splitApiUrl: no path or bare slash yields empty prefix",
          "[adapters.ai][llm_client]") {
    using mondoc::adapters::ai::LlmClient;
    CHECK(LlmClient::splitApiUrl("https://api.openai.com") ==
          std::pair<std::string, std::string>{"https://api.openai.com", ""});
    CHECK(LlmClient::splitApiUrl("https://api.openai.com/") ==
          std::pair<std::string, std::string>{"https://api.openai.com", ""});
    CHECK(LlmClient::splitApiUrl("https://ai.example.com/api/") ==
          std::pair<std::string, std::string>{"https://ai.example.com", "/api"});
}

TEST_CASE("create: URL path overrides the default /v1 prefix",
          "[adapters.ai][llm_client]") {
    using mondoc::adapters::ai::LlmClient;
    auto withPath = LlmClient::create("https://ai.example.com/api", "key");
    REQUIRE(withPath.has_value());
    CHECK((*withPath)->pathPrefix() == "/api");
    CHECK((*withPath)->host() == "https://ai.example.com");

    auto noPath = LlmClient::create("https://ai.example.com", "key");
    REQUIRE(noPath.has_value());
    CHECK((*noPath)->pathPrefix() == "/v1");

    auto loopback = LlmClient::create("http://localhost:8080/v1", "key");
    REQUIRE(loopback.has_value());
    CHECK((*loopback)->host() == "http://localhost:8080");
}
