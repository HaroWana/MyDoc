#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai_fill_pipeline.hpp"
#include "domain/confidence.hpp"
#include "domain/field.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
#include "mondoc/id.hpp"

#include "support/fake_llm_client.hpp"

namespace {

using mondoc::adapters::ai::AiFillPipeline;
using mondoc::adapters::ai::AiFillSourceDoc;
using mondoc::adapters::ai::LlmConfig;
using mondoc::adapters::ai::LlmError;
using mondoc::adapters::ai::RefineInput;
using mondoc::adapters::ai::RunInput;
using mondoc::domain::Confidence;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;
using mondoc::FieldId;
using mondoc::SourceDocId;
using mondoc::TemplateId;
using mondoc::tests_support::FakeLlmClient;
using mondoc::tests_support::makeChatCompletion;

Template threeFieldTemplate() {
    Template tpl;
    tpl.id_ = TemplateId{"tpl-1"};
    tpl.name_ = "patient";
    tpl.fields_ = {
        Field{FieldId{"f1"}, "name",  FieldType::Text},
        Field{FieldId{"f2"}, "dob",   FieldType::Date},
        Field{FieldId{"f3"}, "score", FieldType::Number},
    };
    return tpl;
}

std::vector<AiFillSourceDoc> oneSource() {
    return {AiFillSourceDoc{SourceDocId{"src-0"}, "report.txt",
                            "John Doe born 1985-03-12 scored 95"}};
}

LlmConfig minimalConfig() {
    return LlmConfig{"https://hub.example/v1", "sk-test", "gpt-4o-2026"};
}

}  // namespace

TEST_CASE("run: Pass 1 + Pass 2 fills every template field with confidence",
          "[adapters.ai][pipeline][fill-08][fill-09][fill-10]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"facts", nlohmann::json::array({
            nlohmann::json{{"source_index", 0}, {"char_start", 0}, {"char_end", 8},
                           {"excerpt", "John Doe"}, {"summary", "name"}},
            nlohmann::json{{"source_index", 0}, {"char_start", 32}, {"char_end", 34},
                           {"excerpt", "95"}, {"summary", "score"}},
        })}
    }));
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"fills", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","John Doe"},
                           {"confidence","high"},{"fact_index",0}},
            nlohmann::json{{"field_id","f2"},{"value","1985-3-12"},
                           {"confidence","medium"},{"fact_index",0}},
            nlohmann::json{{"field_id","f3"},{"value","95 points"},
                           {"confidence","low"},{"fact_index",1}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();

    std::atomic<bool> cancelled{false};
    auto result = pipe.run(in, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
    REQUIRE((*result)[0].current_value_ == "John Doe");
    REQUIRE((*result)[1].current_value_ == "1985-03-12");
    REQUIRE((*result)[2].current_value_ == "95");
    for (const auto& f : *result) {
        REQUIRE(f.confidence_ != Confidence::Manual);
        REQUIRE(!f.current_value_.empty());
    }
    REQUIRE(fake.chatCalls_.size() == 2);
}

TEST_CASE("run: cancel flag set between passes returns LlmError::Cancelled",
          "[adapters.ai][pipeline][revw-08]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));

    std::atomic<bool> cancelled{false};
    fake.onAfterCall_ = [&] { cancelled.store(true); };

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();

    auto result = pipe.run(in, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Cancelled);
    REQUIRE(fake.chatCalls_.size() == 1);
}

TEST_CASE("run: empty Pass 1 facts still produces all fields with Low confidence",
          "[adapters.ai][pipeline][fill-11]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"fills", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","unknown"},
                           {"confidence","low"},{"fact_index",-1}},
            nlohmann::json{{"field_id","f2"},{"value","1970-01-01"},
                           {"confidence","low"},{"fact_index",-1}},
            nlohmann::json{{"field_id","f3"},{"value","0"},
                           {"confidence","low"},{"fact_index",-1}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    std::atomic<bool> cancelled{false};

    auto result = pipe.run(in, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
    for (const auto& f : *result) {
        REQUIRE(f.confidence_ == Confidence::Low);
        REQUIRE(!f.current_value_.empty());
    }
}

TEST_CASE("run: LLM omits a field in Pass 2 -> placeholder with Low confidence empty value",
          "[adapters.ai][pipeline][fill-11]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"fills", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","John"},
                           {"confidence","high"},{"fact_index",-1}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    std::atomic<bool> cancelled{false};

    auto result = pipe.run(in, cancelled);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
    REQUIRE((*result)[1].current_value_.empty());
    REQUIRE((*result)[1].confidence_ == Confidence::Low);
    REQUIRE((*result)[2].current_value_.empty());
    REQUIRE((*result)[2].confidence_ == Confidence::Low);
}

TEST_CASE("run: Pass 1 LlmError propagates",
          "[adapters.ai][pipeline][app-06]") {
    FakeLlmClient fake;
    fake.enqueueErr(LlmError::unreachable("test"));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    std::atomic<bool> cancelled{false};

    auto result = pipe.run(in, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Unreachable);
}

TEST_CASE("run: Pass 2 LlmError propagates",
          "[adapters.ai][pipeline][app-06]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));
    fake.enqueueErr(LlmError::rateLimited());

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    std::atomic<bool> cancelled{false};

    auto result = pipe.run(in, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::RateLimited);
}

TEST_CASE("run: Pass 2 non-string field_id/value returns BadResponse without crashing",
          "[adapters.ai][pipeline][sai-4]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"fills", nlohmann::json::array({
            nlohmann::json{{"field_id", 123}, {"value", nlohmann::json{{"x", 1}}}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    std::atomic<bool> cancelled{false};

    auto result = pipe.run(in, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
}

TEST_CASE("run: cancel flag set after Pass 2 chat() returns yields Cancelled",
          "[adapters.ai][pipeline][sai-15]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"facts", nlohmann::json::array()}}));
    fake.enqueueOk(makeChatCompletion(nlohmann::json{{"fills", nlohmann::json::array()}}));

    std::atomic<bool> cancelled{false};
    int callCount = 0;
    fake.onAfterCall_ = [&] {
        ++callCount;
        if (callCount == 2) cancelled.store(true);
    };

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RunInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();

    auto result = pipe.run(in, cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Cancelled);
    REQUIRE(fake.chatCalls_.size() == 2);
}

TEST_CASE("normalizeDateValue rejects impossible dates and zero-pads valid ones (SAI-19)",
          "[adapters.ai][pipeline][sai-19]") {
    REQUIRE(AiFillPipeline::normalizeDateValue("2024-99-99") == "2024-99-99");
    REQUIRE(AiFillPipeline::normalizeDateValue("2024-12-31") == "2024-12-31");
    REQUIRE(AiFillPipeline::normalizeDateValue("2024-3-5") == "2024-03-05");
    REQUIRE(AiFillPipeline::normalizeDateValue("2024-13-01") == "2024-13-01");
    REQUIRE(AiFillPipeline::normalizeDateValue("2024-01-32") == "2024-01-32");
}

TEST_CASE("normalizeNumberValue keeps a single leading minus and a single decimal point (SAI-19)",
          "[adapters.ai][pipeline][sai-19]") {
    REQUIRE(AiFillPipeline::normalizeNumberValue("1.2.3-") == "1.23");
    REQUIRE(AiFillPipeline::normalizeNumberValue("-5") == "-5");
    REQUIRE(AiFillPipeline::normalizeNumberValue("5-3") == "53");
    REQUIRE(AiFillPipeline::normalizeNumberValue("95 points") == "95");
}

TEST_CASE("refine: returns only the fields named in the LLM response",
          "[adapters.ai][pipeline][refine]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","new"},{"confidence","high"}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.current_fills_ = {
        Fill{FieldId{"f1"}, "old", Confidence::High,   {}},
        Fill{FieldId{"f2"}, "old", Confidence::Medium, {}},
        Fill{FieldId{"f3"}, "old", Confidence::Low,    {}},
    };
    in.user_message_ = "make name new";

    auto result = pipe.refine(in, nullptr);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    REQUIRE((*result)[0].field_id_.value() == "f1");
    REQUIRE((*result)[0].current_value_ == "new");
    REQUIRE((*result)[0].confidence_ == Confidence::High);
    REQUIRE(fake.chatCalls_.size() == 1);
}

TEST_CASE("refine: empty updates array returns empty vector",
          "[adapters.ai][pipeline][refine]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array()}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.user_message_ = "no-op";

    auto result = pipe.refine(in, nullptr);

    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE("refine: normalizes date format on update",
          "[adapters.ai][pipeline][refine][fill-09]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array({
            nlohmann::json{{"field_id","f2"},{"value","1985-3-12"},{"confidence","medium"}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.user_message_ = "format date";

    auto result = pipe.refine(in, nullptr);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    REQUIRE((*result)[0].current_value_ == "1985-03-12");
}

TEST_CASE("refine: empty user_message returns BadResponse without chat call",
          "[adapters.ai][pipeline][refine]") {
    FakeLlmClient fake;
    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.user_message_ = "";

    auto result = pipe.refine(in, nullptr);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
    REQUIRE(fake.chatCalls_.empty());
}

TEST_CASE("refine: LLM error propagates and only one chat call is made (Pitfall 5)",
          "[adapters.ai][pipeline][refine]") {
    FakeLlmClient fake;
    fake.enqueueErr(LlmError::unreachable("offline"));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.user_message_ = "anything";

    auto result = pipe.refine(in, nullptr);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Unreachable);
    REQUIRE(fake.chatCalls_.size() == 1);
}

TEST_CASE("refine: non-string field_id/value returns BadResponse without crashing",
          "[adapters.ai][pipeline][refine][sai-4]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array({
            nlohmann::json{{"field_id", 123}, {"value", nlohmann::json{{"x", 1}}}},
        })}
    }));

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.user_message_ = "anything";

    auto result = pipe.refine(in, nullptr);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
}

TEST_CASE("refine: cancel flag set after chat() returns yields Cancelled",
          "[adapters.ai][pipeline][refine][sai-15]") {
    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array()}
    }));
    std::atomic<bool> cancelled{false};
    fake.onAfterCall_ = [&] { cancelled.store(true); };

    AiFillPipeline pipe(fake, minimalConfig());
    Template tpl = threeFieldTemplate();
    RefineInput in;
    in.tpl_ = &tpl;
    in.sources_ = oneSource();
    in.user_message_ = "anything";

    auto result = pipe.refine(in, &cancelled);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Cancelled);
}
