#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ai_fill_pipeline.hpp"
#include "mondoc/id.hpp"

namespace {

using mondoc::adapters::ai::AiFillPipeline;
using mondoc::adapters::ai::AiFillSourceDoc;

constexpr const char* kFixture =
    "The patient John Doe was born on 1985-03-12 in Berlin.";

std::vector<AiFillSourceDoc> oneSource() {
    return {AiFillSourceDoc{mondoc::SourceDocId{"src-0"}, "report.txt", kFixture}};
}

std::string wrapAsChatCompletion(const std::string& contentJson) {
    nlohmann::json envelope = {
        {"choices", nlohmann::json::array({
            nlohmann::json{{"message", nlohmann::json{{"content", contentJson}}}}
        })}
    };
    return envelope.dump();
}

}  // namespace

TEST_CASE("validatePass1Facts: accepts exact-offset fact",
          "[adapters.ai][pass1][fill-07]") {
    const std::string inner = R"({"facts":[{"source_index":0,"char_start":12,"char_end":20,"excerpt":"John Doe","summary":"name"}]})";
    auto facts = AiFillPipeline::validatePass1Facts(wrapAsChatCompletion(inner), oneSource());
    REQUIRE(facts.size() == 1);
    REQUIRE(facts[0].excerpt_ == "John Doe");
    REQUIRE(facts[0].char_start_ == 12);
    REQUIRE(facts[0].char_end_ == 20);
}

TEST_CASE("validatePass1Facts: corrects via find() when offsets are wrong but excerpt matches",
          "[adapters.ai][pass1][fill-07]") {
    const std::string inner = R"({"facts":[{"source_index":0,"char_start":3,"char_end":11,"excerpt":"John Doe","summary":"name"}]})";
    auto facts = AiFillPipeline::validatePass1Facts(wrapAsChatCompletion(inner), oneSource());
    REQUIRE(facts.size() == 1);
    REQUIRE(facts[0].excerpt_ == "John Doe");
    REQUIRE(facts[0].char_start_ == 12);
    REQUIRE(facts[0].char_end_ == 20);
}

TEST_CASE("validatePass1Facts: drops fact when excerpt not in source",
          "[adapters.ai][pass1][fill-07]") {
    const std::string inner = R"({"facts":[{"source_index":0,"char_start":0,"char_end":10,"excerpt":"Jane Smith","summary":"name"}]})";
    auto facts = AiFillPipeline::validatePass1Facts(wrapAsChatCompletion(inner), oneSource());
    REQUIRE(facts.empty());
}

TEST_CASE("validatePass1Facts: drops fact with out-of-range source_index",
          "[adapters.ai][pass1][fill-07]") {
    const std::string inner = R"({"facts":[{"source_index":5,"char_start":12,"char_end":20,"excerpt":"John Doe","summary":"name"}]})";
    auto facts = AiFillPipeline::validatePass1Facts(wrapAsChatCompletion(inner), oneSource());
    REQUIRE(facts.empty());
}

TEST_CASE("validatePass1Facts: returns empty on malformed JSON",
          "[adapters.ai][pass1][fill-07]") {
    auto facts = AiFillPipeline::validatePass1Facts("not json {", oneSource());
    REQUIRE(facts.empty());
}

TEST_CASE("validatePass1Facts: returns empty when response lacks choices[0].message.content",
          "[adapters.ai][pass1][fill-07]") {
    auto facts = AiFillPipeline::validatePass1Facts("{}", oneSource());
    REQUIRE(facts.empty());
}
