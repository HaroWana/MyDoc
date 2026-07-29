#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "canvas_geometry.hpp"
#include "domain/field.hpp"

TEST_CASE("toPixels/toNormalized round-trip", "[ui.geometry]") {
    mondoc::domain::PdfLocation loc{2, 0.25, 0.5, 0.1, 0.05};
    auto px = mondoc::ui::toPixels(loc, 800, 1131);
    auto back = mondoc::ui::toNormalized(2, px, 800, 1131);
    REQUIRE(back.page_index == 2);
    REQUIRE(back.x == Catch::Approx(0.25).margin(0.002));
    REQUIRE(back.h == Catch::Approx(0.05).margin(0.002));
}

TEST_CASE("hitTest: corners, edges, inside, outside", "[ui.geometry]") {
    mondoc::ui::PixelRect f{100, 100, 200, 80};
    using HZ = mondoc::ui::HitZone;
    REQUIRE(mondoc::ui::hitTest(f, 100, 100, 6) == HZ::TopLeft);
    REQUIRE(mondoc::ui::hitTest(f, 300, 180, 6) == HZ::BottomRight);
    REQUIRE(mondoc::ui::hitTest(f, 200, 100, 6) == HZ::Top);
    REQUIRE(mondoc::ui::hitTest(f, 100, 140, 6) == HZ::Left);
    REQUIRE(mondoc::ui::hitTest(f, 200, 140, 6) == HZ::Inside);
    REQUIRE(mondoc::ui::hitTest(f, 50, 50, 6)   == HZ::None);
}
