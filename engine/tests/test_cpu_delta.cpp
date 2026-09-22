#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/CpuDelta.h"

using namespace pulse;

TEST_CASE("first sample yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    REQUIRE_FALSE(delta.update(100, 12450, 1000).has_value());
}

TEST_CASE("second sample matches spec worked example", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 12450, 1000);

    const auto pct = delta.update(100, 12574, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(0.775, 0.0001));
}

TEST_CASE("full saturation of every core reads as 100 percent", "[cpu]") {
    CpuDelta delta(4);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 4000, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(100.0, 0.0001));
}

TEST_CASE("result is clamped when cpu time exceeds wall time times cores", "[cpu]") {
    CpuDelta delta(2);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 9999, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(100.0, 0.0001));
}

TEST_CASE("a backwards counter yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 5000, 1000);

    REQUIRE_FALSE(delta.update(100, 4000, 2000).has_value());
}

TEST_CASE("a non advancing clock yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 5000, 1000);

    REQUIRE_FALSE(delta.update(100, 6000, 1000).has_value());
}

TEST_CASE("pids are tracked independently", "[cpu]") {
    CpuDelta delta(10);
    delta.update(1, 0, 1000);
    delta.update(2, 0, 1000);

    const auto a = delta.update(1, 1000, 2000);
    const auto b = delta.update(2, 500, 2000);

    REQUIRE_THAT(*a, Catch::Matchers::WithinAbs(10.0, 0.0001));
    REQUIRE_THAT(*b, Catch::Matchers::WithinAbs(5.0, 0.0001));
}

TEST_CASE("forget drops the stored sample", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 12450, 1000);
    delta.forget(100);

    REQUIRE_FALSE(delta.update(100, 12574, 2000).has_value());
}

TEST_CASE("zero core count is treated as one core", "[cpu]") {
    CpuDelta delta(0);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 500, 2000);

    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}
