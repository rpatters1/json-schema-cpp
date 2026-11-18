// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

#include "json/access.hpp"

namespace ja = jsonschema::detail::json_access;
using json = nlohmann::json;

TEST_CASE("json_access: kind_of")
{
    CHECK(ja::kind_of(json(nullptr)) == ja::kind::null);
    CHECK(ja::kind_of(json(true)) == ja::kind::boolean);
    CHECK(ja::kind_of(json(1)) == ja::kind::number);
    CHECK(ja::kind_of(json(1u)) == ja::kind::number);
    CHECK(ja::kind_of(json(1.5)) == ja::kind::number);
    CHECK(ja::kind_of(json("s")) == ja::kind::string);
    CHECK(ja::kind_of(json::array()) == ja::kind::array);
    CHECK(ja::kind_of(json::object()) == ja::kind::object);
    CHECK(ja::kind_of(json::binary({1, 2})) == ja::kind::unsupported);
}

TEST_CASE("json_access: is_integral")
{
    CHECK(ja::is_integral(json(0)));
    CHECK(ja::is_integral(json(-7)));
    CHECK(ja::is_integral(json(std::numeric_limits<std::uint64_t>::max())));
    CHECK(ja::is_integral(json(1.0)));
    CHECK(ja::is_integral(json(-0.0)));
    CHECK(ja::is_integral(json::parse("1e2")));
    CHECK(ja::is_integral(json(1e300)));
    CHECK_FALSE(ja::is_integral(json(1.5)));
    CHECK_FALSE(ja::is_integral(json(std::numeric_limits<double>::infinity())));
    CHECK_FALSE(ja::is_integral(json("1")));
    CHECK_FALSE(ja::is_integral(json(true)));
}

TEST_CASE("json_access: compare_numbers is exact across representations")
{
    using std::partial_ordering;
    const auto cmp = [](const json& a, const json& b) { return ja::compare_numbers(a, b); };

    CHECK(cmp(json(1), json(1.0)) == partial_ordering::equivalent);
    CHECK(cmp(json(1u), json(1)) == partial_ordering::equivalent);
    CHECK(cmp(json(-1), json(1u)) == partial_ordering::less);
    CHECK(cmp(json(1u), json(-1)) == partial_ordering::greater);
    CHECK(cmp(json(2), json(1.5)) == partial_ordering::greater);
    CHECK(cmp(json(1.5), json(2)) == partial_ordering::less);
    CHECK(cmp(json(-2), json(-1.5)) == partial_ordering::less);
    CHECK(cmp(json(-1), json(-1.5)) == partial_ordering::greater);

    // 2^53 + 1 is not representable as a double; it must not compare equal to 2^53.
    const std::int64_t big = (std::int64_t{1} << 53) + 1;
    CHECK(cmp(json(big), json(9007199254740992.0)) == partial_ordering::greater);
    CHECK(cmp(json(9007199254740992.0), json(big)) == partial_ordering::less);

    // Values at and beyond the 64-bit integer limits.
    const auto umax = std::numeric_limits<std::uint64_t>::max();
    CHECK(cmp(json(umax), json(18446744073709551616.0)) == partial_ordering::less);
    CHECK(cmp(json(std::numeric_limits<std::int64_t>::max()), json(9223372036854775808.0)) == partial_ordering::less);
    CHECK(cmp(json(std::numeric_limits<std::int64_t>::min()), json(-9223372036854775808.0)) == partial_ordering::equivalent);
    CHECK(cmp(json(std::numeric_limits<std::int64_t>::min()), json(-1e19)) == partial_ordering::greater);
    CHECK(cmp(json(0u), json(-0.5)) == partial_ordering::greater);
    CHECK(cmp(json(0u), json(0.5)) == partial_ordering::less);
}

TEST_CASE("json_access: equal follows JSON Schema equality")
{
    CHECK(ja::equal(json(1), json(1.0)));
    CHECK(ja::equal(json(0), json(-0.0)));
    CHECK_FALSE(ja::equal(json(1), json(true)));
    CHECK_FALSE(ja::equal(json(0), json(false)));
    CHECK_FALSE(ja::equal(json(nullptr), json(false)));
    CHECK_FALSE(ja::equal(json("1"), json(1)));
    CHECK(ja::equal(json::parse(R"([1, {"a": 2.0}])"), json::parse(R"([1.0, {"a": 2}])")));
    CHECK_FALSE(ja::equal(json::parse("[1, 2]"), json::parse("[2, 1]")));
    CHECK(ja::equal(json::parse(R"({"a": 1, "b": [true]})"), json::parse(R"({"b": [true], "a": 1.0})")));
    CHECK_FALSE(ja::equal(json::parse(R"({"a": 1})"), json::parse(R"({"a": 1, "b": 2})")));
    CHECK_FALSE(ja::equal(json::parse(R"({"a": 1})"), json::parse(R"({"b": 1})")));
    CHECK_FALSE(ja::equal(json::array(), json::object()));
}

TEST_CASE("json_access: hash is consistent with equal")
{
    const json pairs[][2] = {
        {json(1), json(1.0)},
        {json(1u), json(1)},
        {json(0), json(-0.0)},
        {json(-5), json(-5.0)},
        {json(std::numeric_limits<std::uint64_t>::max() - 2047), json(18446744073709549568.0)},
        {json::parse(R"({"a": [1, 2.0], "b": null})"), json::parse(R"({"b": null, "a": [1.0, 2]})")},
    };
    for (const auto& pair : pairs) {
        CAPTURE(pair[0].dump());
        REQUIRE(ja::equal(pair[0], pair[1]));
        CHECK(ja::hash(pair[0]) == ja::hash(pair[1]));
    }
    CHECK(ja::hash(json(1)) != ja::hash(json(true)));
    CHECK(ja::hash(json::parse("[1, 2]")) != ja::hash(json::parse("[2, 1]")));
}

TEST_CASE("json_access: code_point_count")
{
    CHECK(ja::code_point_count("") == 0);
    CHECK(ja::code_point_count("abc") == 3);
    CHECK(ja::code_point_count("\xC3\xA9") == 1);          // é
    CHECK(ja::code_point_count("\xE2\x82\xAC") == 1);      // €
    CHECK(ja::code_point_count("\xF0\x9F\x92\xA9") == 1);  // U+1F4A9, a surrogate pair in UTF-16
    CHECK(ja::code_point_count("a\xF0\x9F\x92\xA9" "b") == 3);
}
