// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// `format` (design doc 5.1): when it asserts, unknown formats, custom checkers, and grammar
// edge cases beyond the test suite.
#include <doctest/doctest.h>

#include <string>

#include <jsonschema/jsonschema.hpp>

#include "formats.hpp"

namespace jd = jsonschema::detail;
using jsonschema::compile_options;
using jsonschema::format_mode;
using jsonschema::schema;
using jsonschema::schema_error;
using json = nlohmann::json;

TEST_CASE("format: annotation by default, assertion when asked")
{
    const json root = json::parse(R"({"format": "date"})");
    CHECK(schema::compile(root).is_valid(json("not a date")));
    CHECK_FALSE(schema::compile(root, {.formats = format_mode::assert}).is_valid(json("not a date")));
    CHECK(schema::compile(root, {.formats = format_mode::assert}).is_valid(json("2026-11-18")));
    // Built-in formats ignore other types.
    CHECK(schema::compile(root, {.formats = format_mode::assert}).is_valid(json(42)));
}

TEST_CASE("format: the format-assertion vocabulary turns assertion on")
{
    compile_options options;
    options.resources.push_back({"https://example.com/meta", json::parse(R"({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://example.com/meta",
        "$vocabulary": {"https://json-schema.org/draft/2020-12/vocab/core": true,
                        "https://json-schema.org/draft/2020-12/vocab/format-assertion": true}
    })")});
    const auto s = schema::compile(json::parse(R"({"$schema": "https://example.com/meta", "format": "ipv4"})"), options);
    CHECK(s.is_valid(json("192.168.0.1")));
    CHECK_FALSE(s.is_valid(json("192.168.0.256")));
}

TEST_CASE("format: unknown formats are an error only when asserting")
{
    const json root = json::parse(R"({"format": "colour"})");
    CHECK(schema::compile(root).is_valid(json("red")));
    try {
        static_cast<void>(schema::compile(root, {.formats = format_mode::assert}));
        FAIL("expected schema_error");
    } catch (const schema_error& e) {
        CHECK(std::string(e.what()).find("unknown format \"colour\"") != std::string::npos);
    }
}

TEST_CASE("format: custom checkers, including replacing a built-in")
{
    compile_options options{.formats = format_mode::assert};
    options.extra_formats.emplace_back("colour", [](const json& v) { return v.is_string() && v.get<std::string>().starts_with('#'); });
    options.extra_formats.emplace_back("email", [](const json& v) { return v.is_string() && v.get<std::string>().ends_with("@example.com"); });
    const auto colour = schema::compile(json::parse(R"({"format": "colour"})"), options);
    CHECK(colour.is_valid(json("#ff0000")));
    CHECK_FALSE(colour.is_valid(json("red")));
    CHECK_FALSE(colour.is_valid(json(1)));     // custom checkers see every type
    const auto email = schema::compile(json::parse(R"({"format": "email"})"), options);
    CHECK(email.is_valid(json("a@example.com")));
    CHECK_FALSE(email.is_valid(json("a@example.org")));
}

TEST_CASE("format: still an annotation when it asserts, and errors name the format")
{
    struct collector : jsonschema::annotation_sink
    {
        int formats = 0;
        void on_annotation(const jsonschema::annotation& a) override { formats += a.keyword == "format"; }
    } annotations;
    const auto s = schema::compile(json::parse(R"({"format": "uuid"})"), {.formats = format_mode::assert});
    CHECK(s.validate(json("8f3c2a10-1b2c-4d5e-8f90-123456789abc"), {.annotations = &annotations}).valid);
    CHECK(annotations.formats == 1);

    jsonschema::collecting_error_sink errors;
    CHECK_FALSE(s.validate(json("8f3c2a10"), {.errors = &errors}).valid);
    REQUIRE(errors.errors.size() == 1);
    CHECK(errors.errors[0].keyword == "format");
    CHECK(errors.errors[0].message == "the value \"8f3c2a10\" is not a valid uuid");
}

TEST_CASE("format grammars: date and time")
{
    CHECK(jd::is_date("2024-02-29"));
    CHECK_FALSE(jd::is_date("2023-02-29"));
    CHECK_FALSE(jd::is_date("1900-02-29"));
    CHECK(jd::is_date("2000-02-29"));
    CHECK_FALSE(jd::is_date("2026-1-01"));
    CHECK(jd::is_time("23:59:60Z"));
    CHECK(jd::is_time("15:59:60-08:00"));        // 23:59:60 UTC
    CHECK_FALSE(jd::is_time("23:59:60+01:00"));  // 22:59:60 UTC
    CHECK_FALSE(jd::is_time("12:00:00"));        // the offset is required
    CHECK(jd::is_time("12:00:00.123456z"));
    CHECK(jd::is_date_time("2026-11-18t10:00:00+05:30"));
    CHECK_FALSE(jd::is_date_time("2026-11-18 10:00:00Z"));
}

TEST_CASE("format grammars: duration")
{
    CHECK(jd::is_duration("P1Y2M3DT4H5M6S"));
    CHECK(jd::is_duration("P2W"));
    CHECK(jd::is_duration("PT36H"));
    CHECK_FALSE(jd::is_duration("P"));
    CHECK_FALSE(jd::is_duration("PT"));
    CHECK_FALSE(jd::is_duration("P1D2Y"));       // out of order
    CHECK_FALSE(jd::is_duration("P1Y2D"));       // Y then D needs M between them
    CHECK_FALSE(jd::is_duration("P1W1D"));       // weeks stand alone
    CHECK_FALSE(jd::is_duration("P1.5D"));       // whole numbers only
}

TEST_CASE("format grammars: email, hostname, uri-template, relative-json-pointer")
{
    CHECK(jd::is_email("first.last@example.com"));
    CHECK(jd::is_email("\"a b\"@example.com"));
    CHECK(jd::is_email("user@[192.168.0.1]"));
    CHECK(jd::is_email("user@[IPv6:::1]"));
    CHECK_FALSE(jd::is_email("first..last@example.com"));
    CHECK_FALSE(jd::is_email(std::string(65, 'a') + "@example.com"));

    CHECK(jd::is_hostname("www.example.com"));
    CHECK(jd::is_hostname("xn--bcher-kva.example"));
    CHECK_FALSE(jd::is_hostname("ab--cd.example"));   // "--" in positions 3-4 is reserved for xn--
    CHECK_FALSE(jd::is_hostname("-a.example"));
    CHECK_FALSE(jd::is_hostname(std::string(64, 'a') + ".example"));

    CHECK(jd::is_uri_template("http://example.com/{user}/{+path}{?q,lang}"));
    CHECK(jd::is_uri_template("{var:3}{list*}"));
    CHECK_FALSE(jd::is_uri_template("{var:0}"));
    CHECK_FALSE(jd::is_uri_template("{unterminated"));

    CHECK(jd::is_relative_json_pointer("0"));
    CHECK(jd::is_relative_json_pointer("1/a~1b"));
    CHECK(jd::is_relative_json_pointer("2#"));
    CHECK_FALSE(jd::is_relative_json_pointer("01"));
    CHECK_FALSE(jd::is_relative_json_pointer("-1/a"));
}

TEST_CASE("format grammars: iri")
{
    CHECK(jd::is_iri("http://\xC3\xA9xample.org/\xE2\x82\xAC"));        // éxample, €
    CHECK(jd::is_iri("http://example.org/?q=\xEE\x80\x80"));            // U+E000, private use: query only
    CHECK_FALSE(jd::is_iri("http://example.org/\xEE\x80\x80"));
    CHECK_FALSE(jd::is_iri("//example.org"));                           // no scheme
    CHECK(jd::is_iri_reference("//example.org/\xC3\xA9"));
    CHECK_FALSE(jd::is_iri_reference("\xC3"));                          // ill-formed UTF-8
}
