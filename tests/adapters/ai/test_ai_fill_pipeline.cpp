#include <catch2/catch_test_macros.hpp>

#include "ai_fill_pipeline.hpp"

TEST_CASE("AiFillPipeline::run integrates Pass 1 + Pass 2 via FakeLlmClient",
          "[adapters.ai][pipeline][skip-until-impl]") {
    REQUIRE(true);
}
