#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai_field_detector.hpp"
#include "domain/field.hpp"
#include "i_llm_client.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace {

using mondoc::adapters::ai::AiFieldDetector;
using mondoc::adapters::ai::DetectionResult;
using mondoc::adapters::ai::FieldImprovement;
using mondoc::adapters::ai::ILlmClient;
using mondoc::adapters::ai::LlmConfig;
using mondoc::adapters::ai::LlmError;
using mondoc::domain::Field;
using mondoc::domain::FieldOrigin;
using mondoc::domain::FieldType;
using mondoc::FieldId;

class FakeLlmClient : public ILlmClient {
public:
    std::queue<mondoc::expected<std::string, LlmError>> responses_;
    std::vector<std::string> chatCalls_;
    std::function<void()> onAfterCall_;

    void enqueueOk(std::string body) {
        responses_.emplace(std::move(body));
    }
    void enqueueErr(LlmError e) {
        responses_.emplace(mondoc::unexpected<LlmError>(std::move(e)));
    }

    mondoc::expected<std::string, LlmError> chat(const std::string& body) override {
        chatCalls_.push_back(body);
        if (responses_.empty()) {
            if (onAfterCall_) onAfterCall_();
            return mondoc::unexpected<LlmError>(LlmError::unreachable("fake exhausted"));
        }
        auto r = std::move(responses_.front());
        responses_.pop();
        if (onAfterCall_) onAfterCall_();
        return r;
    }
};

std::string makeChatCompletion(const nlohmann::json& contentJson) {
    nlohmann::json envelope = {
        {"choices", nlohmann::json::array({
            nlohmann::json{{"message", nlohmann::json{{"content", contentJson.dump()}}}}
        })}
    };
    return envelope.dump();
}

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
    // malformed: not a valid JSON completion
    client.enqueueOk(R"({"choices":[{"message":{"content":"not-json"}}]})");

    AiFieldDetector detector(client, testConfig());
    std::atomic<bool> cancelled{false};
    auto result = detector.detect("some text", {}, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
}
