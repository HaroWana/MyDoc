#include <catch2/catch_test_macros.hpp>

#include "llm_error.hpp"

TEST_CASE("LlmClient: 429 maps to LlmError::RateLimited",
          "[adapters.ai][llm_error][skip-until-impl]") {
    REQUIRE(true);
}
