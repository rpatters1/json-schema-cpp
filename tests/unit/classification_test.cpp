// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Schema classification (design doc 4), on a small schema shaped like MNX: a `type`
// discriminator chooses among `anyOf` branches, one branch is the default when `type` is
// absent, shared attributes come in through `allOf`, and a dictionary uses
// `additionalProperties`.
#include <doctest/doctest.h>

#include <algorithm>
#include <iterator>
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
    struct candidate
    {
        std::string location;
        std::string name;
        via reached_by;
        confidence certainty;
        bool declares_id;
    };
    struct pending_branch
    {
        jsonschema::classification_branch_id parent;
        std::vector<candidate> candidates;
    };
    std::map<jsonschema::classification_branch_id, pending_branch> pending;
    std::vector<std::string> visits;

    void accept(jsonschema::classification_branch_id branch, candidate item)
    {
        if (branch) {
            pending.at(branch).candidates.push_back(std::move(item));
            return;
        }
        const auto status = static_cast<classification_status>(item.certainty);
        auto it = std::find_if(rows.begin(), rows.end(), [&](const seen& row) { return row.location == item.location; });
        if (it == rows.end()) {
            rows.push_back({item.location, {}, status});
            it = std::prev(rows.end());
        }
        it->status = std::max(it->status, status);
        static const char* const vias[] = {"root", "child", "ref", "in_place"};
        static const char* const confidences[] = {"exact", "recovered", "ambiguous"};
        it->names.push_back(item.name + "/" + vias[static_cast<int>(item.reached_by)] + "/" +
                            confidences[static_cast<int>(item.certainty)]);
        if (item.location == "/content/0" && item.name == "event") id_declared = item.declares_id;
    }
    void on_classified(const jsonschema::classification& c) override
    {
        REQUIRE(c.matches.size() == 1);
        const auto& m = c.matches.front();
        visits.emplace_back(m.name);
        accept(c.branch, {c.instance_location.to_string(), std::string(m.name), m.reached_by,
                          m.confidence, m.info.declares_property("id")});
    }
    void on_branch(const jsonschema::classification_branch& event) override
    {
        using jsonschema::branch_action;
        REQUIRE(event.id != 0);
        REQUIRE((event.parent == 0 || pending.contains(event.parent)));
        if (event.action == branch_action::begin) {
            REQUIRE(pending.emplace(event.id, pending_branch{event.parent, {}}).second);
            return;
        }
        auto branch = pending.extract(event.id);
        REQUIRE(!branch.empty());
        CHECK(branch.mapped().parent == event.parent);
        if (event.action == branch_action::commit) {
            for (auto& item : branch.mapped().candidates) {
                if (event.ambiguous_at && item.location == event.ambiguous_at->to_string()) {
                    item.certainty = confidence::ambiguous;
                }
                accept(event.parent, std::move(item));
            }
        }
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

    CHECK(r.rows.size() == 4);
    CHECK(r.at("/content") == nullptr);

    const seen* event = r.at("/content/0");
    REQUIRE(event != nullptr);
    CHECK(event->status == classification_status::exact);
    // A named node reports its own outcome after its nested matches.
    CHECK(event->names == std::vector<std::string>{"attrs/in_place/exact", "event/in_place/exact", "content/ref/exact"});
    CHECK(r.id_declared);

    CHECK(r.at("/content/1")->names == std::vector<std::string>{"attrs/in_place/exact", "grace/in_place/exact", "content/ref/exact"});
    CHECK(r.at("/content/1/notes/0")->names == std::vector<std::string>{"attrs/in_place/exact", "event/ref/exact"});
    CHECK(r.at("/sounds/id")->names == std::vector<std::string>{"sound/ref/exact"});
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
    CHECK(r.at("/content/0") == nullptr);

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

TEST_CASE("classification: strict recovery")
{
    const auto s = compile_music({.enabled = true, .recovery = jsonschema::recovery_policy::none()});
    recorder valid;
    CHECK(s.validate(json::parse(R"({"content": [{"duration": 1}]})"), {.classifications = &valid}).valid);
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

TEST_CASE("classification: checking all matching alternatives is opt-in")
{
    const auto alternatives = json::parse(R"({
        "$defs":{"a":{}, "b":{}, "c":{}},
        "anyOf":[{"$ref":"#/$defs/a"},{"$ref":"#/$defs/b"},{"$ref":"#/$defs/c"}]
    })");
    for (const bool all : {false, true}) {
        CAPTURE(all);
        for (const bool exclusive : {false, true}) {
            auto definition = alternatives;
            if (exclusive) {
                definition["oneOf"] = definition.at("anyOf");
                definition.erase("anyOf");
            }
            const auto s = schema::compile(definition, {.classification = {.enabled = true}});
            recorder r;
            CHECK(s.validate(json::object(), {.classifications = &r, .check_all_matches = all}).valid == !exclusive);
            REQUIRE(r.rows.size() == 1);
            const std::size_t minimum_checked = exclusive ? 2 : 1;
            CHECK(r.rows[0].names.size() == (all ? 3 : minimum_checked));
        }
    }
}

TEST_CASE("classification: contains and unevaluated keywords retain the required matches")
{
    auto definition = json::parse(R"({"$defs":{"n":{"type":"integer"}}, "contains":{"$ref":"#/$defs/n"}})");
    for (bool needs_all : {false, true}) {
        if (needs_all) {
            definition["unevaluatedItems"] = false;
        }
        const auto s = schema::compile(definition, {.classification = {.enabled = true}});
        for (bool all : {false, true}) {
            recorder r;
            CHECK(s.validate(json::array({1, 2, 3}), {.classifications = &r, .check_all_matches = all}).valid);
            CHECK(r.rows.size() == ((all || needs_all) ? 3 : 1));
        }
    }

    const auto s = schema::compile(json::parse(R"({
        "$defs":{"a":{}, "b":{}},
        "anyOf":[{"properties":{"x/y":{"$ref":"#/$defs/a"}}},{"properties":{"~":{"$ref":"#/$defs/b"}}}],
        "unevaluatedProperties":false
    })"), {.classification = {.enabled = true}});
    recorder r;
    CHECK(s.validate(json::parse(R"({"x/y":1,"~":2})"), {.classifications = &r}).valid);
    REQUIRE(r.rows.size() == 2);
    CHECK(r.at("/x~1y") != nullptr);
    CHECK(r.at("/~0") != nullptr);
}

TEST_CASE("classification: required annotations report overlapping visits at the correct location")
{
    struct ignore_annotations : jsonschema::annotation_sink {
        void on_annotation(const jsonschema::annotation&) override {}
    } annotations;
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"first":{}, "second":{}},
        "anyOf":[{"properties":{"a/b":{"$ref":"#/$defs/first"}}},
                 {"properties":{"a/b":{"allOf":[{"$ref":"#/$defs/first"},{"$ref":"#/$defs/second"}]}}}]
    })"), {.classification = {.enabled = true}});
    recorder r;
    CHECK(s.validate(json::parse(R"({"a/b":42,"ignored":{"child":{}}})"),
                     {.annotations = &annotations, .classifications = &r}).valid);
    REQUIRE(r.rows.size() == 1);
    CHECK(r.rows.front().location == "/a~1b");
    CHECK(r.rows.front().names == std::vector<std::string>{"first/ref/exact", "first/in_place/exact", "second/in_place/exact"});
}

TEST_CASE("classification: branch outcomes are independent of checking all alternatives")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"early":{},"winner":{},"late":{}},
        "anyOf":[{"allOf":[{"$ref":"#/$defs/early"},false]},{"$ref":"#/$defs/winner"},{"$ref":"#/$defs/late"}]
    })"), {.classification = {.enabled = true}});
    for (bool all : {false, true}) {
        recorder r;
        CHECK(s.validate(json::object(), {.classifications = &r, .check_all_matches = all}).valid);
        REQUIRE(r.rows.size() == 1);
        CHECK(r.pending.empty());
        std::vector<std::string> expected_visits{"early", "winner"};
        std::vector<std::string> expected_kept{"winner/in_place/exact"};
        if (all) {
            expected_visits.emplace_back("late");
            expected_kept.emplace_back("late/in_place/exact");
        }
        CHECK(r.visits == expected_visits);
        CHECK(r.rows[0].names == expected_kept);
    }
}

TEST_CASE("classification: a late identity failure discards earlier child matches")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"named":{}}, "properties":{"a":{"$ref":"#/$defs/named"},"z":{"const":"required"}}
    })"), {.classification = {.enabled = true}});
    recorder r;
    CHECK_FALSE(s.validate(json::parse(R"({"a":1,"z":"wrong"})"), {.classifications=&r}).valid);
    CHECK(r.visits == std::vector<std::string>{"named"});
    CHECK(r.rows.empty());
    CHECK(r.pending.empty());
}

TEST_CASE("classification: committed callbacks can run before the next sibling is validated")
{
    recorder r;
    jsonschema::compile_options options;
    options.classification.enabled = true;
    options.formats = jsonschema::format_mode::assert;
    bool observed = false;
    options.extra_formats.emplace_back("observe", [&](const json&) {
        observed = r.at("/first") != nullptr && r.pending.empty();
        return true;
    });
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"chosen":{}},
        "properties":{"first":{"anyOf":[{"$ref":"#/$defs/chosen"}]},"next":{"format":"observe"}}
    })"), options);
    CHECK(s.validate(json::parse(R"({"first":1,"next":"x"})"), {.classifications=&r}).valid);
    CHECK(observed);
}

TEST_CASE("classification: synthetic property names and diagnostic replays do not emit provisional visits")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"named":{}},
        "propertyNames":{"$ref":"#/$defs/named"},
        "anyOf":[{"allOf":[{"$ref":"#/$defs/named"},false]}]
    })"), {.classification={.enabled=true}});
    recorder r;
    jsonschema::collecting_error_sink errors;
    CHECK_FALSE(s.validate(json::parse(R"({"key":1})"),
        {.errors=&errors,.classifications=&r}).valid);
    REQUIRE(r.rows.size() == 1);
    CHECK(r.rows.front().names.size() == 1);
    CHECK(r.visits.size() == 1);
    CHECK(r.pending.empty());
}

TEST_CASE("classification: a committed inner alternative is discarded with its outer branch")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"inner":{},"kept":{}},
        "anyOf":[
            {"allOf":[{"anyOf":[{"properties":{"x":{"$ref":"#/$defs/inner"}}}]},false]},
            {"$ref":"#/$defs/kept"}
        ]
    })"), {.classification={.enabled=true}});
    recorder r;
    CHECK(s.validate(json::parse(R"({"x":1})"), {.classifications=&r}).valid);
    CHECK(r.visits == std::vector<std::string>{"inner", "kept"});
    CHECK(r.pending.empty());
    REQUIRE(r.rows.size() == 1);
    CHECK(r.rows.front().location.empty());
    CHECK(r.rows.front().names == std::vector<std::string>{"kept/in_place/exact"});
}

TEST_CASE("classification: oneOf ambiguity affects only the branch's own location")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"obj":{"properties":{"x":{"$ref":"#/$defs/child"}}},"child":{},"other":{}},
        "oneOf":[{"$ref":"#/$defs/obj"},{"$ref":"#/$defs/other"}]
    })"), {.classification={.enabled=true}});
    recorder r;
    CHECK_FALSE(s.validate(json::parse(R"({"x":1})"), {.classifications=&r}).valid);
    CHECK(r.pending.empty());
    REQUIRE(r.at("/x"));
    CHECK(r.at("/x")->status == classification_status::exact);
    REQUIRE(r.at(""));
    CHECK(r.at("")->names == std::vector<std::string>{"obj/in_place/ambiguous","other/in_place/ambiguous"});
}

TEST_CASE("classification: failed if probes and nonmatching contains items discard their local matches")
{
    const auto s = schema::compile(json::parse(R"({
        "$defs":{"visited":{},"fallback":{}},
        "if":{"allOf":[{"$ref":"#/$defs/visited"},false]},
        "else":{"$ref":"#/$defs/fallback"},
        "contains":{"allOf":[{"$ref":"#/$defs/visited"},{"type":"integer"}]}
    })"), {.classification = {.enabled = true}});
    recorder r;
    CHECK(s.validate(json::parse(R"(["wrong",42])"), {.classifications = &r}).valid);
    CHECK(r.pending.empty());
    CHECK(r.visits == std::vector<std::string>{"visited","fallback","visited","visited"});
    CHECK(r.at("/0") == nullptr);
    REQUIRE(r.at("/1"));
    CHECK(r.at("/1")->names == std::vector<std::string>{"visited/in_place/exact"});
    REQUIRE(r.at(""));
    CHECK(r.at("")->names == std::vector<std::string>{"fallback/in_place/exact"});
}
