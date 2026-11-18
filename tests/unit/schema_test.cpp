// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Behaviour of schema::compile and schema::is_valid that the official test suite does
// not cover: errors, dialects, loading, base URIs.
#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <jsonschema/jsonschema.hpp>

using jsonschema::compile_options;
using jsonschema::schema;
using jsonschema::schema_error;
using json = nlohmann::json;

namespace {

std::string compile_error(const json& root, compile_options options = {})
{
    try {
        static_cast<void>(schema::compile(root, std::move(options)));
    } catch (const schema_error& e) {
        return e.what();
    }
    return {};
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("schema: basic validation")
{
    const auto s = schema::compile(json::parse(R"({
        "type": "object",
        "properties": {"id": {"type": "string"}, "count": {"type": "integer", "minimum": 0}},
        "required": ["id"]
    })"));
    CHECK(s.is_valid(json::parse(R"({"id": "a", "count": 3})")));
    CHECK(s.is_valid(json::parse(R"({"id": "a", "count": 3.0})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"count": 3})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"id": "a", "count": -1})")));
    CHECK_FALSE(s.is_valid(json::parse(R"([])")));
}

TEST_CASE("schema: boolean root schemas")
{
    CHECK(schema::compile(json(true)).is_valid(json(1)));
    CHECK_FALSE(schema::compile(json(false)).is_valid(json(1)));
    CHECK(contains(compile_error(json(1)), "object or a boolean"));
}

TEST_CASE("schema: base URI")
{
    CHECK(schema::compile(json::parse(R"({"$id": "HTTP://Example.com/a/../s.json"})")).base_uri().to_string() ==
          "http://example.com/s.json");
    CHECK(schema::compile(json::object(), {.base_uri = "https://example.com/root"}).base_uri().to_string() ==
          "https://example.com/root");
    CHECK(schema::compile(json::parse(R"({"$id": "child.json"})"), {.base_uri = "https://example.com/dir/root"})
              .base_uri()
              .to_string() == "https://example.com/dir/child.json");

    const std::string generated = schema::compile(json::object()).base_uri().to_string();
    CHECK(generated.starts_with("urn:uuid:"));
    CHECK(generated.size() == 9 + 36);
    CHECK(generated[9 + 14] == '4');

    CHECK(contains(compile_error(json::object(), {.base_uri = "relative/path"}), "absolute"));
}

TEST_CASE("schema: dialects")
{
    CHECK(compile_error(json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema"})")).empty());
    CHECK(compile_error(json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema#"})")).empty());

    const json draft7 = json::parse(R"({"$schema": "http://json-schema.org/draft-07/schema#"})");
    CHECK(contains(compile_error(draft7), "unsupported dialect"));
    CHECK(compile_error(draft7, {.accept_other_dialects = true}).empty());

    // A custom meta-schema whose own dialect is 2020-12.
    const json custom = json::parse(R"({"$schema": "https://example.com/meta", "minimum": 1})");
    CHECK(contains(compile_error(custom), "unknown meta-schema"));
    compile_options with_meta;
    with_meta.resources.push_back(
        {"https://example.com/meta", json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema"})")});
    const auto s = schema::compile(custom, with_meta);
    CHECK_FALSE(s.is_valid(json(0)));

    // An embedded resource may not switch to another draft either.
    CHECK(contains(compile_error(json::parse(R"({
        "$defs": {"old": {"$id": "https://example.com/old", "$schema": "http://json-schema.org/draft-04/schema#"}},
        "$ref": "https://example.com/old"
    })")),
                   "unsupported dialect"));
}

TEST_CASE("schema: invalid schemas fail meta-schema validation")
{
    try {
        static_cast<void>(schema::compile(json::parse(R"({"$id": "https://example.com/m", "$defs": {"x": {"required": "a"}}})")));
        FAIL("expected schema_error");
    } catch (const schema_error& e) {
        const std::string what = e.what();
        CHECK(contains(what, "https://example.com/m is not valid against its meta-schema https://json-schema.org/draft/2020-12/schema"));
        CHECK(contains(what, "at /$defs/x/required: expected array, got string"));
        REQUIRE(e.metaschema_errors().size() == 1);
        CHECK(e.metaschema_errors()[0].instance_location.to_string() == "/$defs/x/required");
        CHECK(e.metaschema_errors()[0].instance == nullptr);
    }

    // Referenced documents are validated too.
    compile_options options;
    options.resources.push_back({"https://example.com/bad", json::parse(R"({"minLength": -1})")});
    CHECK(contains(compile_error(json::parse(R"({"$ref": "https://example.com/bad"})"), options),
                   "https://example.com/bad is not valid against its meta-schema"));
    // Unreferenced ones are not.
    CHECK(compile_error(json::object(), options).empty());

    // `$ref` to the meta-schema itself works without a loader: it is built in.
    const auto meta = schema::compile(json::parse(R"({"$ref": "https://json-schema.org/draft/2020-12/schema"})"));
    CHECK(meta.is_valid(json::parse(R"({"type": "string"})")));
    CHECK_FALSE(meta.is_valid(json::parse(R"({"type": "strin"})")));
}

TEST_CASE("schema: $vocabulary")
{
    compile_options options;
    options.resources.push_back({"https://example.com/no-validation", json::parse(R"({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://example.com/no-validation",
        "$vocabulary": {"https://json-schema.org/draft/2020-12/vocab/core": true,
                        "https://json-schema.org/draft/2020-12/vocab/applicator": true,
                        "https://example.com/vocab/optional-extra": false},
        "allOf": [{"$ref": "https://json-schema.org/draft/2020-12/meta/core"},
                  {"$ref": "https://json-schema.org/draft/2020-12/meta/applicator"}]
    })")});
    const auto s = schema::compile(json::parse(R"({
        "$schema": "https://example.com/no-validation",
        "properties": {"n": {"minimum": 10}},
        "type": "string"
    })"),
                                   options);
    // Validation keywords are unknown here, so only the applicators apply.
    CHECK(s.is_valid(json::parse(R"({"n": 1})")));

    options.resources.push_back({"https://example.com/needs-extra", json::parse(R"({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://example.com/needs-extra",
        "$vocabulary": {"https://json-schema.org/draft/2020-12/vocab/core": true, "https://example.com/vocab/extra": true}
    })")});
    CHECK(contains(compile_error(json::parse(R"({"$schema": "https://example.com/needs-extra"})"), options),
                   "requires the vocabulary https://example.com/vocab/extra"));
}

TEST_CASE("schema: invalid keyword values are compile errors")
{
    // The compiler's own checks, for when meta-schema validation is off.
    const auto error_for = [](const char* text) { return compile_error(json::parse(text), {.validate_against_metaschema = false}); };
    CHECK(contains(error_for(R"({"type": "strin"})"), "#/type"));
    CHECK(contains(error_for(R"({"type": ["string", "string"]})"), "unique"));
    CHECK(contains(error_for(R"({"minLength": -1})"), "non-negative integer"));
    CHECK(contains(error_for(R"({"minLength": 1.5})"), "non-negative integer"));
    CHECK(error_for(R"({"minLength": 1.0})").empty());
    CHECK(contains(error_for(R"({"multipleOf": 0})"), "greater than 0"));
    CHECK(contains(error_for(R"({"maximum": "1"})"), "number"));
    CHECK(contains(error_for(R"({"allOf": []})"), "non-empty array"));
    CHECK(contains(error_for(R"({"allOf": [1]})"), "#/allOf"));
    CHECK(contains(error_for(R"({"items": [{}]})"), "prefixItems"));
    CHECK(contains(error_for(R"({"properties": {"a": 1}})"), "expected a schema"));
    CHECK(contains(error_for(R"({"required": "a"})"), "array of strings"));
    CHECK(contains(error_for(R"({"pattern": "("})"), "invalid regular expression"));
    CHECK(contains(error_for(R"({"patternProperties": {"(": {}}, "additionalProperties": false})"), "#/patternProperties"));
    CHECK(contains(error_for(R"({"contains": {}, "minContains": -1})"), "#/minContains"));
    CHECK(contains(error_for(R"({"$id": "#frag"})"), "invalid $id"));
    CHECK(contains(error_for(R"({"$anchor": "1bad"})"), "invalid $anchor"));
    CHECK(contains(error_for(R"({"$ref": "#/$defs/missing"})"), "matches nothing"));
    CHECK(contains(error_for(R"({"$ref": "#nowhere"})"), "no anchor"));
    CHECK(contains(error_for(R"({"$ref": "https://example.com/unknown"})"), "no loader is set"));
    CHECK(contains(error_for(R"({"$defs": {"a": 1}, "$ref": "#/$defs/a"})"), "does not identify a schema"));
}

TEST_CASE("schema: unknown keywords and annotations are ignored")
{
    const auto s = schema::compile(json::parse(R"({"title": "t", "x-custom": {"type": "string"}, "format": "email"})"));
    CHECK(s.is_valid(json(1)));
    CHECK(s.is_valid(json("not an email")));
}

TEST_CASE("schema: loader")
{
    std::vector<std::string> requested;
    compile_options options;
    options.loader = [&](const jsonschema::uri& u) -> std::optional<json> {
        requested.push_back(u.to_string());
        if (u.to_string() == "https://example.com/positive.json") {
            return json::parse(R"({"$defs": {"p": {"exclusiveMinimum": 0}}})");
        }
        return std::nullopt;
    };
    const auto s = schema::compile(json::parse(R"({"$ref": "https://EXAMPLE.com/a/../positive.json#/$defs/p"})"), options);
    CHECK(requested == std::vector<std::string>{"https://example.com/positive.json"});
    CHECK(s.is_valid(json(1)));
    CHECK_FALSE(s.is_valid(json(0)));

    CHECK(contains(compile_error(json::parse(R"({"$ref": "https://example.com/missing.json"})"), options),
                   "the loader could not load it"));

    compile_options throwing;
    throwing.loader = [](const jsonschema::uri&) -> std::optional<json> { throw std::runtime_error("network down"); };
    CHECK(contains(compile_error(json::parse(R"({"$ref": "https://example.com/x.json"})"), throwing), "network down"));
}

TEST_CASE("schema: resources are resolved by retrieval URI and by $id")
{
    compile_options options;
    options.resources.push_back(
        {"https://example.com/retrieved.json", json::parse(R"({"$id": "https://example.com/declared.json", "type": "string"})")});
    CHECK(schema::compile(json::parse(R"({"$ref": "https://example.com/retrieved.json"})"), options).is_valid(json("x")));
    CHECK(schema::compile(json::parse(R"({"$ref": "https://example.com/declared.json"})"), options).is_valid(json("x")));
    CHECK_FALSE(schema::compile(json::parse(R"({"$ref": "https://example.com/declared.json"})"), options).is_valid(json(1)));

    CHECK(contains(compile_error(json::object(), {.resources = {{"relative.json", json::object()}}}), "absolute"));
    CHECK(contains(compile_error(json::parse(R"({"$defs": {"a": {"$id": "https://x.org/s"}, "b": {"$id": "https://x.org/s"}}})")),
                   "share the URI"));
}

TEST_CASE("schema: reference cycles that never consume the instance are rejected")
{
    CHECK(contains(compile_error(json::parse(R"({"$ref": "#"})")), "infinite recursion"));
    CHECK(contains(compile_error(json::parse(R"({"$defs": {"a": {"allOf": [{"$ref": "#/$defs/b"}]}, "b": {"not": {"$ref": "#/$defs/a"}}},
                                                 "$ref": "#/$defs/a"})")),
                   "infinite recursion"));
    // Recursion through a child applicator consumes the instance and is fine.
    const auto tree = schema::compile(json::parse(R"({"type": "object", "additionalProperties": {"$ref": "#"}})"));
    CHECK(tree.is_valid(json::parse(R"({"a": {"b": {}}})")));
    CHECK_FALSE(tree.is_valid(json::parse(R"({"a": {"b": 1}})")));
}

TEST_CASE("schema: $dynamicRef recursion at one instance location is an evaluation_error")
{
    const auto s = schema::compile(json::parse(R"({"$dynamicAnchor": "a", "$dynamicRef": "#a"})"));
    CHECK_THROWS_AS(static_cast<void>(s.is_valid(json(1))), jsonschema::evaluation_error);
}

TEST_CASE("schema: definitions is treated like $defs")
{
    // Identifiers inside `definitions` are found without a JSON-pointer reference.
    const auto by_id = schema::compile(json::parse(R"({
        "definitions": {"s": {"$id": "https://example.com/s", "type": "string"}},
        "$ref": "https://example.com/s"
    })"));
    CHECK(by_id.is_valid(json("x")));
    CHECK_FALSE(by_id.is_valid(json(1)));

    const auto by_anchor = schema::compile(json::parse(R"({
        "definitions": {"n": {"$anchor": "number", "type": "number"}},
        "properties": {"a": {"$ref": "#number"}}
    })"));
    CHECK(by_anchor.is_valid(json::parse(R"({"a": 1})")));
    CHECK_FALSE(by_anchor.is_valid(json::parse(R"({"a": "1"})")));

    // As with $defs, its contents are not applied to the instance.
    CHECK(schema::compile(json::parse(R"({"definitions": {"x": false}})")).is_valid(json(1)));
}

TEST_CASE("schema: legacy dependencies")
{
    const json root = json::parse(R"({
        "dependencies": {"credit": ["billing"], "debit": {"required": ["account"]}}
    })");
    const auto s = schema::compile(root);
    CHECK(s.is_valid(json::parse(R"({"credit": 1, "billing": 2})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"credit": 1})")));
    CHECK(s.is_valid(json::parse(R"({"debit": 1, "account": 2})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"debit": 1})")));
    CHECK(s.is_valid(json::parse(R"([])")));

    const auto strict = schema::compile(root, {.legacy_dependencies = false});
    CHECK(strict.is_valid(json::parse(R"({"credit": 1})")));
    CHECK(strict.is_valid(json::parse(R"({"debit": 1})")));

    const compile_options unchecked{.validate_against_metaschema = false};
    CHECK(contains(compile_error(json::parse(R"({"dependencies": {"a": [1]}})"), unchecked), "only strings"));
    CHECK(contains(compile_error(json::parse(R"({"dependencies": []})"), unchecked), "#/dependencies"));
    CHECK(compile_error(json::parse(R"({"dependencies": []})"), {.validate_against_metaschema = false, .legacy_dependencies = false}).empty());
    // The 2020-12 meta-schema still describes `dependencies`, so it checks the shape either way.
    CHECK(contains(compile_error(json::parse(R"({"dependencies": []})"), {.legacy_dependencies = false}), "not valid against its meta-schema"));

    // An identifier inside a schema-form dependency is discovered.
    const auto nested = schema::compile(json::parse(R"({
        "dependencies": {"a": {"$id": "https://example.com/dep"}},
        "properties": {"b": {"$ref": "https://example.com/dep"}}
    })"));
    CHECK(nested.is_valid(json::parse(R"({"b": 1})")));
}

TEST_CASE("schema: $dynamicRef resolves to the outermost dynamic anchor")
{
    compile_options options;
    options.resources.push_back({"https://example.com/list", json::parse(R"({
        "$id": "https://example.com/list",
        "type": "array",
        "items": {"$dynamicRef": "#item"},
        "$defs": {"default": {"$dynamicAnchor": "item"}}
    })")});
    const auto strings = schema::compile(json::parse(R"({
        "$id": "https://example.com/strings",
        "$ref": "list",
        "$defs": {"item": {"$dynamicAnchor": "item", "type": "string"}}
    })"),
                                         options);
    CHECK(strings.is_valid(json::parse(R"(["a", "b"])")));
    CHECK_FALSE(strings.is_valid(json::parse(R"(["a", 1])")));

    const auto any = schema::compile(json::parse(R"({"$ref": "https://example.com/list"})"), options);
    CHECK(any.is_valid(json::parse(R"(["a", 1])")));
}

TEST_CASE("schema: number semantics")
{
    const auto integer = schema::compile(json::parse(R"({"type": "integer"})"));
    CHECK(integer.is_valid(json::parse("1e2")));
    CHECK(integer.is_valid(json::parse("18446744073709551615")));
    CHECK_FALSE(integer.is_valid(json::parse("1.5")));

    const auto multiple = schema::compile(json::parse(R"({"multipleOf": 3})"));
    CHECK(multiple.is_valid(json::parse("-9")));
    CHECK(multiple.is_valid(json::parse("18446744073709551615")));
    CHECK_FALSE(multiple.is_valid(json::parse("1e20")));
    CHECK(multiple.is_valid(json::parse("3e20")));

    const auto decimal = schema::compile(json::parse(R"({"multipleOf": 0.01})"));
    CHECK(decimal.is_valid(json::parse("19.99")));
    CHECK_FALSE(decimal.is_valid(json::parse("19.995")));

    const auto bounds = schema::compile(json::parse(R"({"minimum": 9007199254740993, "exclusiveMaximum": 18446744073709551615})"));
    CHECK(bounds.is_valid(json::parse("9007199254740993")));
    CHECK_FALSE(bounds.is_valid(json::parse("9007199254740992")));
    CHECK_FALSE(bounds.is_valid(json::parse("18446744073709551615")));
}

TEST_CASE("schema: strings count code points")
{
    const auto s = schema::compile(json::parse(R"({"minLength": 2, "maxLength": 2})"));
    CHECK(s.is_valid(json("\xC3\xA9\xF0\x9F\x8E\xB5")));    // é + 🎵: 2 code points, 6 bytes
    CHECK_FALSE(s.is_valid(json("\xF0\x9F\x8E\xB5")));
}

TEST_CASE("schema: uniqueItems uses JSON equality on large arrays")
{
    const auto s = schema::compile(json::parse(R"({"uniqueItems": true})"));
    json items = json::array();
    for (int i = 0; i < 100; ++i) {
        items.push_back(json::object({{"k", i}}));
    }
    CHECK(s.is_valid(items));
    items.push_back(json::parse(R"({"k": 50.0})"));
    CHECK_FALSE(s.is_valid(items));
}

TEST_CASE("schema: compiled schemas can be shared between threads")
{
    const auto s = schema::compile(json::parse(R"({
        "$id": "https://example.com/tree",
        "$dynamicAnchor": "node",
        "type": "object",
        "properties": {"value": {"type": "integer"}, "children": {"type": "array", "items": {"$dynamicRef": "#node"}}}
    })"));
    const json good = json::parse(R"({"value": 1, "children": [{"value": 2, "children": []}]})");
    const json bad = json::parse(R"({"value": 1, "children": [{"value": "2"}]})");
    std::vector<std::thread> threads;
    std::vector<int> failures(8, 0);
    for (std::size_t t = 0; t < failures.size(); ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 200; ++i) {
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

TEST_CASE("schema: move-only")
{
    auto a = schema::compile(json::parse(R"({"type": "string"})"));
    schema b = std::move(a);
    CHECK(b.is_valid(json("x")));
    a = schema::compile(json(false));
    CHECK_FALSE(a.is_valid(json("x")));
}
