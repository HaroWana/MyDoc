#include <catch2/catch_test_macros.hpp>

#include "llm_client.hpp"

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
