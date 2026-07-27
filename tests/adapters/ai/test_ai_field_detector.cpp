#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

#include <nlohmann/json.hpp>

#include "ai_field_detector.hpp"
#include "domain/field.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
#include "mondoc/id.hpp"

#include "support/fake_llm_client.hpp"

namespace {

using mondoc::adapters::ai::AiFieldDetector;
using mondoc::adapters::ai::DetectionResult;
using mondoc::adapters::ai::FieldImprovement;
using mondoc::adapters::ai::LlmConfig;
using mondoc::adapters::ai::LlmError;
using mondoc::domain::Field;
using mondoc::domain::FieldOrigin;
using mondoc::domain::FieldType;
using mondoc::FieldId;
using mondoc::tests_support::FakeLlmClient;
using mondoc::tests_support::makeChatCompletion;

LlmConfig testConfig() {
    LlmConfig cfg;
    cfg.api_url = "http://fake";
    cfg.api_key = "key";
    cfg.model   = "test-model";
    return cfg;
}

}  // namespace

TEST_CASE("detect returns new_fields from structured response",
          "[adapters.ai][field_detector][aifd-01][phase06]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array({
            nlohmann::json{{"name", "invoice_number"}, {"type", "text"}}
        })},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("Invoice #12345", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->new_fields.size() == 1);
    REQUIRE(result->new_fields[0].name_ == "invoice_number");
    REQUIRE(result->new_fields[0].type_ == FieldType::Text);
    REQUIRE(result->new_fields[0].origin_ == FieldOrigin::Ai);
}

TEST_CASE("detect returns improvements for existing fields",
          "[adapters.ai][field_detector][aifd-02][phase06]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array()},
        {"improvements", nlohmann::json::array({
            nlohmann::json{
                {"field_name", "f1"},
                {"suggested_name", "invoice_date"},
                {"suggested_type", "date"}
            }
        })}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    Field f;
    f.id_   = FieldId{"f1"};
    f.name_ = "f1";
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("Invoice date: 2025-01-01", {f}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->improvements.size() == 1);
    REQUIRE(result->improvements[0].field_name == "f1");
    REQUIRE(result->improvements[0].suggested_name == "invoice_date");
    REQUIRE(result->improvements[0].suggested_type == "date");
}

TEST_CASE("detect propagates LlmError on transport failure",
          "[adapters.ai][field_detector][aifd-01][phase06]") {
    FakeLlmClient client;
    client.enqueueErr(LlmError::unreachable("network timeout"));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Unreachable);
}

TEST_CASE("detect returns badResponse on malformed content",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    client.enqueueOk(R"({"choices":[{"message":{"content":"not-json"}}]})");

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
}

TEST_CASE("detect returns cancelled when flag is set before call",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{true};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Cancelled);
    REQUIRE(client.chatCalls_.empty());
}

TEST_CASE("detect returns cancelled when flag flips after chat() returns",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    std::atomic<bool> cancelled{false};
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array()},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));
    client.onAfterCall_ = [&cancelled]() { cancelled.store(true); };

    AiFieldDetector detector(client, testConfig());
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Cancelled);
}

TEST_CASE("detect caps new_fields at 200 items",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    nlohmann::json fields = nlohmann::json::array();
    for (int i = 0; i < 250; ++i) {
        fields.push_back({{"name", "field_" + std::to_string(i)}, {"type", "text"}});
    }
    nlohmann::json response = {
        {"new_fields", fields},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->new_fields.size() == 200);
}

TEST_CASE("detect skips new_fields items with empty or missing name",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array({
            nlohmann::json{{"name", ""}, {"type", "text"}},
            nlohmann::json{{"type", "text"}},
            nlohmann::json{{"name", "valid_field"}, {"type", "text"}}
        })},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->new_fields.size() == 1);
    REQUIRE(result->new_fields[0].name_ == "valid_field");
}

TEST_CASE("detect returns badResponse when new_fields has non-string name/type",
          "[adapters.ai][field_detector][sai-5]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array({
            nlohmann::json{{"name", 123}, {"type", true}}
        })},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
}

TEST_CASE("detect caps improvements at 200 items",
          "[adapters.ai][field_detector][sai-6]") {
    FakeLlmClient client;
    nlohmann::json improvements = nlohmann::json::array();
    for (int i = 0; i < 250; ++i) {
        improvements.push_back({{"field_name", "f" + std::to_string(i)},
                                 {"suggested_name", "g" + std::to_string(i)},
                                 {"suggested_type", "text"}});
    }
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array()},
        {"improvements", improvements}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->improvements.size() == 200);
}

TEST_CASE("detect skips improvements items missing required keys",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array()},
        {"improvements", nlohmann::json::array({
            nlohmann::json{{"field_name", "f1"}},
            nlohmann::json{{"field_name", "f2"}, {"suggested_name", "g2"}, {"suggested_type", "text"}}
        })}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->improvements.size() == 1);
    REQUIRE(result->improvements[0].field_name == "f2");
}

TEST_CASE("detect falls back to FieldType::Text for unknown type value",
          "[adapters.ai][field_detector][phase06]") {
    FakeLlmClient client;
    nlohmann::json response = {
        {"new_fields", nlohmann::json::array({
            nlohmann::json{{"name", "some_field"}, {"type", "frobnicate"}}
        })},
        {"improvements", nlohmann::json::array()}
    };
    client.enqueueOk(makeChatCompletion(response));

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->new_fields.size() == 1);
    REQUIRE(result->new_fields[0].type_ == FieldType::Text);
}
