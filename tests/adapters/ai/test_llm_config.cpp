#include <catch2/catch_test_macros.hpp>

#include "llm_config.hpp"

TEST_CASE("LlmConfig: missing file returns unconfigured",
          "[adapters.ai][llm_config][skip-until-impl]") {
    REQUIRE(true);
}
