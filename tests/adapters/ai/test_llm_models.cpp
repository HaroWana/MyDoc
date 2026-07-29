#include <catch2/catch_test_macros.hpp>

#include "llm_client.hpp"

using mondoc::adapters::ai::LlmClient;
using mondoc::adapters::ai::LlmError;

TEST_CASE("parseModelsResponse: valid list returns sorted ids",
          "[adapters.ai][llm_models]") {
    const std::string body = R"({"data":[{"id":"zeta"},{"id":"alpha"},{"id":"mid"}]})";
    auto r = LlmClient::parseModelsResponse(body);
    REQUIRE(r.has_value());
    REQUIRE(*r == std::vector<std::string>{"alpha", "mid", "zeta"});
}

TEST_CASE("parseModelsResponse: entries without a string id are skipped",
          "[adapters.ai][llm_models]") {
    const std::string body =
        R"({"data":[{"id":"good"},{"id":42},{"name":"no-id"},{"id":"also-good"}]})";
    auto r = LlmClient::parseModelsResponse(body);
    REQUIRE(r.has_value());
    REQUIRE(*r == std::vector<std::string>{"also-good", "good"});
}

TEST_CASE("parseModelsResponse: empty data yields empty list",
          "[adapters.ai][llm_models]") {
    auto r = LlmClient::parseModelsResponse(R"({"data":[]})");
    REQUIRE(r.has_value());
    REQUIRE(r->empty());
}

TEST_CASE("parseModelsResponse: missing or non-array data is badResponse",
          "[adapters.ai][llm_models]") {
    for (const char* body : {R"({"models":[]})", R"({"data":"oops"})", R"({})"}) {
        auto r = LlmClient::parseModelsResponse(body);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().kind() == LlmError::Kind::BadResponse);
    }
}

TEST_CASE("parseModelsResponse: malformed JSON is badResponse",
          "[adapters.ai][llm_models]") {
    auto r = LlmClient::parseModelsResponse("not json {");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == LlmError::Kind::BadResponse);
}

TEST_CASE("listModels: unreachable host classified Unreachable",
          "[adapters.ai][llm_models]") {
    auto client = LlmClient::create("http://127.0.0.1:1", "key", "/v1");
    REQUIRE(client.has_value());
    auto r = (*client)->listModels();
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == LlmError::Kind::Unreachable);
}
