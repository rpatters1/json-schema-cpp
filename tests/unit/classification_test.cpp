// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Schema classification (design doc 4), on a small schema shaped like MNX: a `type`
// discriminator chooses among `anyOf` branches, one branch is the default when `type` is
// absent, shared attributes come in through `allOf`, and a dictionary uses
// `additionalProperties`.
#include <doctest/doctest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <jsonschema/jsonschema.hpp>

using jsonschema::classification_status;
using jsonschema::confidence;
using jsonschema::schema;
using jsonschema::via;
using json = nlohmann::json;

namespace {

const char* const music_schema = R"({
    "$id": "https://example.com/music",
    "type": "object",
    "properties": {
        "content": {"type": "array", "items": {"$ref": "#/$defs/content"}},
        "sounds": {"type": "object", "additionalProperties": {"$ref": "#/$defs/sound"}}
    },
    "unevaluatedProperties": false,
    "$defs": {
        "attrs": {"properties": {"id": {"type": "string"}}},
        "content": {"anyOf": [{"$ref": "#/$defs/event"}, {"$ref": "#/$defs/grace"}]},
        "event": {
            "type": "object",
            "allOf": [{"$ref": "#/$defs/attrs"}],
            "properties": {"type": {"const": "event"}, "duration": {"type": "integer", "minimum": 1}},
            "required": ["duration"],
            "unevaluatedProperties": false
        },
        "grace": {
            "type": "object",
            "allOf": [{"$ref": "#/$defs/attrs"}],
            "properties": {"type": {"const": "grace"}, "notes": {"type": "array", "items": {"$ref": "#/$defs/event"}}},
            "required": ["type"],
            "unevaluatedProperties": false
        },
        "sound": {"type": "object", "properties": {"name": {"type": "string"}}}
    }
})";

struct seen
{
    std::string location;
    std::vector<std::string> names;     // "name/via/confidence"
    classification_status status;
};

struct recorder : jsonschema::classification_sink
{
    std::vector<seen> rows;
    bool id_declared = false;
    void on_classified(const jsonschema::classification& c) override
    {
        seen s{c.instance_location.to_string(), {}, c.status};
        for (const auto& m : c.matches) {
            static const char* const vias[] = {"root", "child", "ref", "in_place"};
            static const char* const confidences[] = {"exact", "recovered", "ambiguous"};
            s.names.push_back(std::string(m.name) + "/" + vias[static_cast<int>(m.reached_by)] + "/" +
                              confidences[static_cast<int>(m.confidence)]);
        }
        for (const auto& m : c.matches) {
            if (s.location == "/content/0" && m.name == "event") {
                id_declared = m.info.declares_property("id");   // through allOf → attrs
            }
        }
        rows.push_back(std::move(s));
    }
    [[nodiscard]] const seen* at(const std::string& location) const
    {
        for (const auto& r : rows) {
            if (r.location == location) {
                return &r;
            }
        }
        return nullptr;
    }
};

schema compile_music(jsonschema::classification_options options = {.enabled = true})
{
    return schema::compile(json::parse(music_schema), {.classification = options});
}

} // namespace

TEST_CASE("classification: a valid document")
{
    const auto s = compile_music();
    recorder r;
    const json doc = json::parse(R"({
        "content": [{"id": "e1", "duration": 2}, {"type": "grace", "id": "g1", "notes": [{"duration": 1}]}],
        "sounds": {"id": {"name": "not an id"}}
    })");
    CHECK(s.validate(doc, {.classifications = &r}).valid);

    // Complete walk: every object and array, in document order.
    std::vector<std::string> locations;
    for (const auto& row : r.rows) {
        locations.push_back(row.location);
    }
    CHECK(locations == std::vector<std::string>{"", "/content", "/content/0", "/content/1", "/content/1/notes", "/content/1/notes/0",
                                                "/sounds", "/sounds/id"});

    const seen* event = r.at("/content/0");
    REQUIRE(event != nullptr);
    CHECK(event->status == classification_status::exact);
    // Outermost first: the items' $ref target, then the anyOf branch, then allOf's attrs.
    CHECK(event->names == std::vector<std::string>{"content/ref/exact", "event/in_place/exact", "attrs/in_place/exact"});
    CHECK(r.id_declared);

    CHECK(r.at("/content/1")->names == std::vector<std::string>{"content/ref/exact", "grace/in_place/exact", "attrs/in_place/exact"});
    CHECK(r.at("/content/1/notes/0")->names == std::vector<std::string>{"event/ref/exact", "attrs/in_place/exact"});
    CHECK(r.at("/sounds/id")->names == std::vector<std::string>{"sound/ref/exact"});
    CHECK(r.at("/content")->status == classification_status::unclassified);   // no named schema
}

TEST_CASE("classification: content errors keep the classification")
{
    const auto s = compile_music();
    recorder r;
    jsonschema::collecting_error_sink errors;
    // A stray property and an out-of-range duration: still an event.
    const json doc = json::parse(R"({"content": [{"id": "e1", "duration": 0, "stray": true}]})");
    CHECK_FALSE(s.validate(doc, {.errors = &errors, .classifications = &r}).valid);
    CHECK_FALSE(errors.errors.empty());
    const seen* event = r.at("/content/0");
    REQUIRE(event != nullptr);
    CHECK(event->status == classification_status::recovered);
    CHECK(event->names[1] == "event/in_place/recovered");
}

TEST_CASE("classification: a wrong discriminator is an identity failure")
{
    const auto s = compile_music();
    recorder r;
    // "type": "graec" fails event's const and grace's const: neither applies.
    CHECK_FALSE(s.validate(json::parse(R"({"content": [{"type": "graec", "id": "x"}]})"), {.classifications = &r}).valid);
    const seen* item = r.at("/content/0");
    REQUIRE(item != nullptr);
    CHECK(item->status == classification_status::unclassified);
    CHECK(item->names.empty());

    // A grace note with a content error: the missing-type default (event) is ruled out by
    // the discriminator, so grace is the one plausible branch.
    recorder r2;
    CHECK_FALSE(s.validate(json::parse(R"({"content": [{"type": "grace", "notes": "wrong"}]})"), {.classifications = &r2}).valid);
    const seen* grace = r2.at("/content/0");
    REQUIRE(grace != nullptr);
    CHECK(grace->status == classification_status::recovered);
    CHECK(grace->names[1] == "grace/in_place/recovered");
}

TEST_CASE("classification: ambiguous when several failing branches are plausible")
{
    const auto s = schema::compile(json::parse(R"({
        "anyOf": [{"$ref": "#/$defs/a"}, {"$ref": "#/$defs/b"}],
        "$defs": {"a": {"required": ["x"]}, "b": {"required": ["y"]}}
    })"),
                                   {.classification = {.enabled = true}});
    recorder r;
    CHECK_FALSE(s.validate(json::parse("{}"), {.classifications = &r}).valid);
    REQUIRE(r.at("") != nullptr);
    CHECK(r.at("")->status == classification_status::ambiguous);
    // Through anyOf, then $ref: the strongest edge since the previous match is in_place.
    CHECK(r.at("")->names == std::vector<std::string>{"a/in_place/ambiguous", "b/in_place/ambiguous"});
}

TEST_CASE("classification: strict recovery and no complete walk")
{
    const auto s = compile_music({.enabled = true, .recovery = jsonschema::recovery_policy::none(), .complete_walk = false});
    recorder valid;
    CHECK(s.validate(json::parse(R"({"content": [{"duration": 1}]})"), {.classifications = &valid}).valid);
    // Only classified locations are reported without a complete walk.
    REQUIRE(valid.rows.size() == 1);
    CHECK(valid.rows[0].location == "/content/0");

    recorder invalid;
    CHECK_FALSE(s.validate(json::parse(R"({"content": [{"duration": 0}]})"), {.classifications = &invalid}).valid);
    CHECK(invalid.rows.empty());    // strict: a failing schema classifies nothing
}

TEST_CASE("classification: naming rules")
{
    const json root = json::parse(R"({
        "properties": {"a": {"$anchor": "alpha", "x-class": "Alpha"}, "b": {"x-class": "Beta"}}
    })");
    const auto by_class = jsonschema::namer::custom([](const jsonschema::schema_node_view& v) -> std::optional<std::string> {
        if (const auto it = v.schema.find("x-class"); v.schema.is_object() && it != v.schema.end()) {
            return it->get<std::string>();
        }
        return std::nullopt;
    });
    const auto s = schema::compile(root, {.classification = {.enabled = true, .name_rule = jsonschema::namer::anchors() | by_class}});
    recorder r;
    CHECK(s.validate(json::parse(R"({"a": {}, "b": {}})"), {.classifications = &r}).valid);
    CHECK(r.at("/a")->names == std::vector<std::string>{"alpha/child/exact"});
    CHECK(r.at("/b")->names == std::vector<std::string>{"Beta/child/exact"});
}

TEST_CASE("classification: needs to be enabled at compile time")
{
    const auto s = schema::compile(json::parse(music_schema));
    recorder r;
    CHECK_THROWS_AS(static_cast<void>(s.validate(json::object(), {.classifications = &r})), std::invalid_argument);
}
