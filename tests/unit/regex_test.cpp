// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The regex backend (design doc 5.2). Most behaviour is covered by the suite's
// ecmascript-regex tests; these cover what the suite can't express.
#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

#include <jsonschema/jsonschema.hpp>

#include "regex.hpp"

namespace jd = jsonschema::detail;
using json = nlohmann::json;

TEST_CASE("regex: invalid patterns are compile errors")
{
    CHECK(jd::compile_regex("(") == nullptr);
    CHECK(jd::compile_regex("[") == nullptr);
    CHECK(jd::compile_regex("a{2,1}") == nullptr);
    CHECK(jd::compile_regex("^a+$") != nullptr);
}

TEST_CASE("regex: not anchored")
{
    const auto r = jd::compile_regex("es");
    CHECK(r->search("expression"));
    CHECK_FALSE(r->search("EXPRESSION"));
}

#if defined(JSONSCHEMA_REGEX_SRELL)

TEST_CASE("regex: ECMA-262 with the u flag (SRELL)")
{
    CHECK(jd::compile_regex(R"(^\p{Letter}+$)")->search("\xC3\xA9t\xC3\xA9"));        // été
    CHECK_FALSE(jd::compile_regex(R"(^\p{Letter}+$)")->search("abc1"));
    CHECK(jd::compile_regex("^.$")->search("\xF0\x9F\x8E\xB5"));                       // one code point, four bytes
    CHECK_FALSE(jd::compile_regex(R"(^\d$)")->search("\xDF\x80"));                     // NKO DIGIT ZERO: \d is ASCII only
    CHECK(jd::compile_regex(R"(^\s$)")->search("\xE2\x80\x83"));                       // EM SPACE
    CHECK(jd::compile_regex("(?<=a)b")->search("ab"));                                 // lookbehind
    CHECK(jd::compile_regex("^(?<year>[0-9]{4})$")->search("2026"));                   // named group
}

TEST_CASE("regex: catastrophic backtracking is an evaluation_error, not a hang (SRELL)")
{
    const auto s = jsonschema::schema::compile(json::parse(R"({"pattern": "^(a+)+$"})"));
    const json input(std::string(40, 'a') + "!");
    CHECK_THROWS_AS(static_cast<void>(s.is_valid(input)), jsonschema::evaluation_error);
}

#endif

TEST_CASE("regex: one compiled pattern can be searched from many threads")
{
    const auto s = jsonschema::schema::compile(json::parse(R"({"patternProperties": {"^x-[a-z]+$": {"type": "string"}}})"));
    const json good = json::parse(R"({"x-alpha": "a", "x-beta": "b", "other": 1})");
    const json bad = json::parse(R"({"x-alpha": 1})");
    std::vector<int> failures(8, 0);
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < failures.size(); ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i) {
                if (!s.is_valid(good) || s.is_valid(bad)) {
                    ++failures[t];
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const int f : failures) {
        CHECK(f == 0);
    }
}
