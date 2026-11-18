// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// schema::output: the 2020-12 §12.4 standard output formats.
#include <doctest/doctest.h>

#include <jsonschema/jsonschema.hpp>

using jsonschema::output_format;
using jsonschema::schema;
using json = nlohmann::json;

namespace {

// The example of 2020-12 core §12.5.
const char* const polygon = R"({
    "$id": "https://example.com/polygon",
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "$defs": {
        "point": {
            "type": "object",
            "properties": {"x": {"type": "number"}, "y": {"type": "number"}},
            "additionalProperties": false,
            "required": ["x", "y"]
        }
    },
    "type": "array",
    "items": {"$ref": "#/$defs/point"},
    "minItems": 3
})";

const json failing = json::parse(R"([{"x": 2.5, "y": 1.3}, {"x": 1, "z": 6.7}])");

} // namespace

TEST_CASE("output: flag")
{
    const auto s = schema::compile(json::parse(polygon));
    CHECK(s.output(failing, output_format::flag) == json::parse(R"({"valid": false})"));
    CHECK(s.output(json::parse(R"([{"x":0,"y":0},{"x":1,"y":0},{"x":0,"y":1}])"), output_format::flag) ==
          json::parse(R"({"valid": true})"));
}

TEST_CASE("output: basic is a flat list of error units")
{
    const auto s = schema::compile(json::parse(polygon));
    const json out = s.output(failing);     // basic is the default
    CHECK(out["valid"] == false);
    REQUIRE(out["errors"].size() == 3);
    CHECK(out["errors"][1] == json::parse(R"({
        "valid": false,
        "keywordLocation": "/items/$ref/required",
        "absoluteKeywordLocation": "https://example.com/polygon#/$defs/point/required",
        "instanceLocation": "/1",
        "error": "the required property \"y\" is missing"
    })"));
    CHECK(out["errors"][2]["instanceLocation"] == "/1/z");
    CHECK(out["errors"][0]["keywordLocation"] == "/minItems");
}

TEST_CASE("output: detailed nests by schema structure, as in 2020-12 §12.5")
{
    const auto s = schema::compile(json::parse(polygon));
    const json out = s.output(failing, output_format::detailed);
    CHECK(out["valid"] == false);
    CHECK(out["keywordLocation"] == "");
    CHECK(out["instanceLocation"] == "");
    REQUIRE(out["errors"].size() == 2);
    CHECK(out["errors"][0]["keywordLocation"] == "/minItems");
    CHECK(out["errors"][0].contains("error"));

    // `/items` has a single child, so it collapses into `/items/$ref`, which holds two errors.
    const json& point = out["errors"][1];
    CHECK(point["keywordLocation"] == "/items/$ref");
    CHECK(point["absoluteKeywordLocation"] == "https://example.com/polygon#/$defs/point");
    CHECK(point["instanceLocation"] == "/1");
    REQUIRE(point["errors"].size() == 2);
    CHECK(point["errors"][0]["keywordLocation"] == "/items/$ref/required");
    CHECK(point["errors"][1]["keywordLocation"] == "/items/$ref/additionalProperties");
}

TEST_CASE("output: annotations when valid")
{
    const auto s = schema::compile(json::parse(R"({"$id": "https://example.com/t", "title": "T", "properties": {"a": {"description": "A"}}})"));
    const json basic = s.output(json::parse(R"({"a": 1})"));
    CHECK(basic["valid"] == true);
    REQUIRE(basic["annotations"].size() == 2);
    CHECK(basic["annotations"][1] == json::parse(R"({
        "valid": true,
        "keywordLocation": "/properties/a/description",
        "absoluteKeywordLocation": "https://example.com/t#/properties/a/description",
        "instanceLocation": "/a",
        "annotation": "A"
    })"));

    const json detailed = s.output(json::parse(R"({"a": 1})"), output_format::detailed);
    CHECK(detailed["valid"] == true);
    CHECK(detailed["annotations"].size() == 2);

    // Nothing to report: just the verdict.
    const auto plain = schema::compile(json::parse(R"({"type": "integer"})"));
    CHECK(plain.output(json(1)) == json::parse(R"({"valid": true})"));
}
