// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// schema::validate: errors, annotations, unevaluated*.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <jsonschema/jsonschema.hpp>

using jsonschema::schema;
using json = nlohmann::json;

namespace {

struct error_row
{
    std::string instance_location;
    std::string evaluation_path;
    std::string keyword;
};

std::vector<error_row> errors_of(const schema& s, const json& instance, std::size_t max_errors = 0)
{
    jsonschema::collecting_error_sink sink;
    const auto r = s.validate(instance, {.errors = &sink, .max_errors = max_errors});
    CHECK(r.valid == sink.errors.empty());
    std::vector<error_row> rows;
    for (const auto& e : sink.errors) {
        rows.push_back({e.instance_location.to_string(), e.evaluation_path, std::string(e.keyword)});
    }
    return rows;
}

} // namespace

TEST_CASE("validate: without sinks equals is_valid")
{
    const auto s = schema::compile(json::parse(R"({"type": "object", "required": ["a"]})"));
    CHECK(s.validate(json::parse(R"({"a": 1})")).valid);
    CHECK_FALSE(s.validate(json::parse(R"({})")));
}

TEST_CASE("validate: reports every error with locations")
{
    const auto s = schema::compile(json::parse(R"({
        "$id": "https://example.com/s",
        "type": "object",
        "properties": {"name": {"type": "string"}, "tags": {"type": "array", "items": {"$ref": "#/$defs/tag"}}},
        "required": ["name", "size"],
        "$defs": {"tag": {"type": "string", "minLength": 2}}
    })"));
    jsonschema::collecting_error_sink sink;
    const json instance = json::parse(R"({"name": 5, "tags": ["ok", "x", 3]})");
    CHECK_FALSE(s.validate(instance, {.errors = &sink}).valid);
    REQUIRE(sink.errors.size() == 4);

    // The node's own assertions come first, then its subschemas', in order.
    CHECK(sink.errors[0].keyword == "required");
    CHECK(sink.errors[0].message == "the required property \"size\" is missing");
    CHECK(sink.errors[0].instance_location.to_string().empty());
    CHECK(sink.errors[0].schema_location == "https://example.com/s#/required");

    CHECK(sink.errors[1].instance_location.to_string() == "/name");
    CHECK(sink.errors[1].evaluation_path == "/properties/name/type");
    CHECK(sink.errors[1].message == "expected string, got integer");
    CHECK(*sink.errors[1].instance == json(5));

    // Errors are regular values: they can be copied and assigned.
    jsonschema::collecting_error_sink copy;
    copy = sink;
    CHECK(copy.errors.size() == sink.errors.size());

    CHECK(sink.errors[2].instance_location.to_string() == "/tags/1");
    CHECK(sink.errors[2].evaluation_path == "/properties/tags/items/$ref/minLength");
    CHECK(sink.errors[2].schema_location == "https://example.com/s#/$defs/tag/minLength");

    CHECK(sink.errors[3].instance_location.to_string() == "/tags/2");
    CHECK(sink.errors[3].keyword == "type");
}

TEST_CASE("validate: errors in a passing anyOf branch are not delivered")
{
    const auto s = schema::compile(json::parse(R"({"anyOf": [{"type": "string"}, {"type": "integer"}]})"));
    CHECK(errors_of(s, json(3)).empty());
    const auto rows = errors_of(s, json(1.5));
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].keyword == "anyOf");     // summary first, then each branch
    CHECK(rows[1].evaluation_path == "/anyOf/0/type");
    CHECK(rows[2].evaluation_path == "/anyOf/1/type");
}

TEST_CASE("validate: oneOf, not, if, contains report their own errors")
{
    const auto one_of = schema::compile(json::parse(R"({"oneOf": [{"type": "number"}, {"minimum": 0}]})"));
    const auto both = errors_of(one_of, json(3));
    REQUIRE(both.size() == 1);
    CHECK(both[0].keyword == "oneOf");

    const auto not_schema = schema::compile(json::parse(R"({"not": {"type": "string"}})"));
    REQUIRE(errors_of(not_schema, json("x")).size() == 1);
    CHECK(errors_of(not_schema, json(1)).empty());

    // Errors in `if` itself never count; the failing branch's do.
    const auto conditional = schema::compile(json::parse(R"({"if": {"type": "string"}, "then": {"minLength": 3}, "else": {"minimum": 0}})"));
    const auto then_errors = errors_of(conditional, json("ab"));
    REQUIRE(then_errors.size() == 1);
    CHECK(then_errors[0].evaluation_path == "/then/minLength");
    const auto else_errors = errors_of(conditional, json(-1));
    REQUIRE(else_errors.size() == 1);
    CHECK(else_errors[0].evaluation_path == "/else/minimum");

    // Non-matching items are not errors; only the count is.
    const auto contains = schema::compile(json::parse(R"({"contains": {"type": "string"}, "minContains": 2})"));
    const auto rows = errors_of(contains, json::parse(R"([1, "a", 2])"));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].keyword == "contains");
}

TEST_CASE("validate: rejected members name the property")
{
    const auto s = schema::compile(json::parse(R"({"properties": {"a": {}}, "additionalProperties": false})"));
    jsonschema::collecting_error_sink sink;
    CHECK_FALSE(s.validate(json::parse(R"({"a": 1, "b": 2})"), {.errors = &sink}).valid);
    REQUIRE(sink.errors.size() == 1);
    CHECK(sink.errors[0].instance_location.to_string() == "/b");
    CHECK(sink.errors[0].evaluation_path == "/additionalProperties");
    CHECK(sink.errors[0].message == "the property \"b\" is not allowed here (additionalProperties)");
}

TEST_CASE("validate: max_errors and the ready-made sinks")
{
    const auto s = schema::compile(json::parse(R"({"items": {"type": "string"}})"));
    CHECK(errors_of(s, json::parse("[1, 2, 3]")).size() == 3);
    CHECK(errors_of(s, json::parse("[1, 2, 3]"), 2).size() == 2);

    jsonschema::first_error_sink first;
    CHECK_FALSE(s.validate(json::parse("[1, 2]"), {.errors = &first, .max_errors = 1}).valid);
    REQUIRE(first.has_error());
    CHECK(first.first().instance_location.to_string() == "/0");

    jsonschema::throwing_error_sink thrower;
    try {
        static_cast<void>(s.validate(json::parse("[\"a\", 2]"), {.errors = &thrower}));
        FAIL("expected validation_error");
    } catch (const jsonschema::validation_error& e) {
        CHECK(e.instance_location.to_string() == "/1");
        CHECK(e.keyword == "type");
        CHECK(std::string(e.what()).find("expected string, got integer") != std::string::npos);
    }
}

TEST_CASE("validate: propertyNames errors are reported on the object")
{
    // A property name is not part of the instance, so the error is on the object and keeps
    // referring into the instance after validate returns.
    const auto s = schema::compile(json::parse(R"({"propertyNames": {"maxLength": 3}})"));
    jsonschema::collecting_error_sink sink;
    const json instance = json::parse(R"({"ok": 1, "toolong": 2})");
    CHECK_FALSE(s.validate(instance, {.errors = &sink}).valid);
    REQUIRE(sink.errors.size() == 1);
    CHECK(sink.errors[0].instance == &instance);
    CHECK(sink.errors[0].evaluation_path == "/propertyNames");
    CHECK(sink.errors[0].message == "the property name \"toolong\" is not valid: the string has 7 characters, more than the maximum of 3");
}

TEST_CASE("validate: unevaluatedProperties sees through $ref, allOf and passing branches")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs": {"base": {"properties": {"id": {"type": "string"}}}},
        "allOf": [{"$ref": "#/$defs/base"}],
        "anyOf": [{"properties": {"a": true}, "required": ["a"]}, {"properties": {"b": true}, "required": ["b"]}],
        "properties": {"kind": {}},
        "unevaluatedProperties": false
    })"));
    CHECK(s.is_valid(json::parse(R"({"id": "x", "a": 1, "kind": 0})")));
    CHECK(s.is_valid(json::parse(R"({"a": 1, "b": 2})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"a": 1, "c": 2})")));
    // The anyOf branch for "b" failed, so "b" is unevaluated.
    CHECK_FALSE(s.is_valid(json::parse(R"({"a": 1, "b": 2, "zz": 0})")));

    const auto rows = errors_of(s, json::parse(R"({"a": 1, "c": 2})"));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].instance_location == "/c");
    CHECK(rows[0].keyword == "unevaluatedProperties");
}

TEST_CASE("validate: unevaluatedItems with prefixItems and contains")
{
    const auto s = schema::compile(json::parse(R"({
        "prefixItems": [{"type": "string"}], "contains": {"type": "integer"}, "unevaluatedItems": false
    })"));
    CHECK(s.is_valid(json::parse(R"(["a", 1, 2])")));
    CHECK_FALSE(s.is_valid(json::parse(R"(["a", 1, true])")));
}

TEST_CASE("validate: annotations")
{
    struct collector : jsonschema::annotation_sink
    {
        std::vector<std::string> seen;
        void on_annotation(const jsonschema::annotation& a) override
        {
            seen.push_back(a.instance_location.to_string() + " " + std::string(a.keyword) + "=" + a.value.dump() + " @" + a.schema_location);
        }
    };
    const auto s = schema::compile(json::parse(R"({
        "$id": "https://example.com/a",
        "title": "Root",
        "properties": {"x": {"description": "X", "x-unit": "mm"}},
        "anyOf": [{"type": "object", "title": "Passing"}, {"type": "string", "title": "Failing"}],
        "not": {"type": "string", "title": "Never"}
    })"));
    collector c;
    CHECK(s.validate(json::parse(R"({"x": 1})"), {.annotations = &c}).valid);
    // Evaluation order: in-place applicators (anyOf) before child applicators (properties).
    CHECK(c.seen == std::vector<std::string>{
                        " title=\"Root\" @https://example.com/a#/title",
                        " title=\"Passing\" @https://example.com/a#/anyOf/0/title",
                        "/x description=\"X\" @https://example.com/a#/properties/x/description",
                        "/x x-unit=\"mm\" @https://example.com/a#/properties/x/x-unit",
                    });

    collector failing;
    CHECK_FALSE(s.validate(json("s"), {.annotations = &failing}).valid);
    CHECK(failing.seen.empty());
}

TEST_CASE("validate: schema locations are percent-encoded fragments")
{
    const auto s = schema::compile(json::parse(R"({"$id": "https://example.com/p", "patternProperties": {"^a b": {"type": "string"}}})"));
    jsonschema::collecting_error_sink sink;
    CHECK_FALSE(s.validate(json::parse(R"({"a bc": 1})"), {.errors = &sink}).valid);
    REQUIRE(sink.errors.size() == 1);
    CHECK(sink.errors[0].schema_location == "https://example.com/p#/patternProperties/%5Ea%20b/type");
    CHECK(sink.errors[0].evaluation_path == "/patternProperties/^a b/type");
}
