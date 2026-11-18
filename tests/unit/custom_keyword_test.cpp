// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Custom keywords (design doc 3.5).
#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>     // doctest prints std::string_view, which MSVC needs the full ostream for
#include <stdexcept>
#include <string>
#include <vector>

#include <jsonschema/jsonschema.hpp>

using jsonschema::compile_options;
using jsonschema::custom_keyword;
using jsonschema::keyword_compile_scope;
using jsonschema::keyword_eval_scope;
using jsonschema::keyword_extension;
using jsonschema::schema;
using jsonschema::schema_error;
using jsonschema::subschema;
using json = nlohmann::json;

namespace {

/// "multipleOfAll": [2, 3] -- an assertion on integers.
class multiple_of_all final : public custom_keyword
{
public:
    explicit multiple_of_all(std::vector<std::int64_t> divisors) : divisors_(std::move(divisors)) {}
    bool evaluate(const json& instance, keyword_eval_scope& scope) const override
    {
        if (!instance.is_number_integer()) {
            return true;
        }
        for (const auto d : divisors_) {
            if (instance.get<std::int64_t>() % d != 0) {
                if (scope.reporting_errors()) {
                    scope.fail(std::to_string(instance.get<std::int64_t>()) + " is not a multiple of " + std::to_string(d));
                }
                return false;
            }
        }
        return true;
    }

private:
    std::vector<std::int64_t> divisors_;
};

keyword_extension multiple_of_all_extension(std::string vocabulary = {})
{
    return {"multipleOfAll", std::move(vocabulary), [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
                if (!scope.value().is_array()) {
                    scope.fail("multipleOfAll must be an array of positive integers");
                }
                std::vector<std::int64_t> divisors;
                for (const auto& d : scope.value()) {
                    if (!d.is_number_integer() || d.get<std::int64_t>() <= 0) {
                        throw std::invalid_argument("multipleOfAll must contain positive integers");
                    }
                    divisors.push_back(d.get<std::int64_t>());
                }
                return divisors.empty() ? nullptr : std::make_unique<multiple_of_all>(std::move(divisors));
            }};
}

/// "both": [A, B] -- applies both in place (like allOf).
class both final : public custom_keyword
{
public:
    both(subschema a, subschema b) : a_(a), b_(b) {}
    bool evaluate(const json& /*instance*/, keyword_eval_scope& scope) const override
    {
        const bool first = scope.apply(a_);
        const bool second = scope.apply(b_);
        return first && second;
    }

private:
    subschema a_, b_;
};

/// "either": [A, B] -- passes if one passes (like anyOf), reporting its own error only.
class either final : public custom_keyword
{
public:
    either(subschema a, subschema b) : a_(a), b_(b) {}
    bool evaluate(const json& /*instance*/, keyword_eval_scope& scope) const override
    {
        if (scope.test(a_) || scope.test(b_)) {
            return true;
        }
        scope.fail("matches neither alternative");
        return false;
    }

private:
    subschema a_, b_;
};

/// "eachValue": S -- applies S to every member value of an object.
class each_value final : public custom_keyword
{
public:
    explicit each_value(subschema s) : s_(s) {}
    bool evaluate(const json& instance, keyword_eval_scope& scope) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        for (auto it = instance.begin(); it != instance.end(); ++it) {
            valid = scope.apply(s_, it.key()) && valid;
        }
        return valid;
    }

private:
    subschema s_;
};

compile_options applicators()
{
    compile_options options;
    options.extra_keywords.push_back({"both", {}, [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
                                          const json& v = scope.value();
                                          return std::make_unique<both>(scope.in_place_subschema(v.at(0)), scope.in_place_subschema(v.at(1)));
                                      }});
    options.extra_keywords.push_back({"either", {}, [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
                                          const json& v = scope.value();
                                          return std::make_unique<either>(scope.in_place_subschema(v.at(0)), scope.in_place_subschema(v.at(1)));
                                      }});
    options.extra_keywords.push_back({"eachValue", {}, [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
                                          return std::make_unique<each_value>(scope.child_subschema(scope.value()));
                                      }});
    return options;
}

std::vector<jsonschema::error> errors_of(const schema& s, const json& instance)
{
    jsonschema::collecting_error_sink sink;
    static_cast<void>(s.validate(instance, {.errors = &sink}));
    return sink.errors;
}

std::string compile_error(const json& root, compile_options options)
{
    try {
        static_cast<void>(schema::compile(root, std::move(options)));
    } catch (const schema_error& e) {
        return e.what();
    }
    return {};
}

} // namespace

TEST_CASE("custom keywords: an assertion")
{
    compile_options options;
    options.extra_keywords.push_back(multiple_of_all_extension());
    const auto s = schema::compile(json::parse(R"({"properties": {"n": {"multipleOfAll": [2, 3]}}})"), options);
    CHECK(s.is_valid(json::parse(R"({"n": 12})")));
    CHECK(s.is_valid(json::parse(R"({"n": "text"})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"n": 8})")));

    const auto errors = errors_of(s, json::parse(R"({"n": 8})"));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].keyword == "multipleOfAll");
    CHECK(errors[0].message == "8 is not a multiple of 3");
    CHECK(errors[0].instance_location.to_string() == "/n");
    CHECK(errors[0].evaluation_path == "/properties/n/multipleOfAll");
    CHECK(errors[0].schema_location.ends_with("#/properties/n/multipleOfAll"));

    // Without the extension it is an unknown keyword: an annotation only.
    CHECK(schema::compile(json::parse(R"({"multipleOfAll": [3]})")).is_valid(json(8)));
    // compile may return nullptr: nothing to evaluate.
    CHECK(schema::compile(json::parse(R"({"multipleOfAll": []})"), options).is_valid(json(7)));
}

TEST_CASE("custom keywords: compile errors and registration")
{
    compile_options options;
    options.extra_keywords.push_back(multiple_of_all_extension());
    std::string message = compile_error(json::parse(R"({"multipleOfAll": 3})"), options);
    CHECK(message.find("multipleOfAll must be an array") != std::string::npos);
    CHECK(message.find("#/multipleOfAll") != std::string::npos);
    message = compile_error(json::parse(R"({"multipleOfAll": [0]})"), options);   // another exception type
    CHECK(message.find("multipleOfAll must contain positive integers") != std::string::npos);

    compile_options clash;
    clash.extra_keywords.push_back({"minimum", {}, [](const keyword_compile_scope&) { return std::unique_ptr<custom_keyword>(); }});
    CHECK(compile_error(json::object(), clash).find("is a JSON Schema 2020-12 keyword") != std::string::npos);
    compile_options twice;
    twice.extra_keywords = {multiple_of_all_extension(), multiple_of_all_extension()};
    CHECK(compile_error(json::object(), twice).find("registered twice") != std::string::npos);
    compile_options empty;
    empty.extra_keywords.push_back({"x", {}, {}});
    CHECK(compile_error(json::object(), empty).find("compile is not set") != std::string::npos);
}

TEST_CASE("custom keywords: still an annotation")
{
    struct collector : jsonschema::annotation_sink
    {
        int count = 0;
        void on_annotation(const jsonschema::annotation& a) override { count += a.keyword == "multipleOfAll"; }
    } annotations;
    compile_options options;
    options.extra_keywords.push_back(multiple_of_all_extension());
    const auto s = schema::compile(json::parse(R"({"multipleOfAll": [2]})"), options);
    CHECK(s.validate(json(4), {.annotations = &annotations}).valid);
    CHECK(annotations.count == 1);
}

TEST_CASE("custom keywords: in-place subschemas")
{
    const auto s = schema::compile(json::parse(R"({
        "both": [{"type": "integer"}, {"$ref": "#/$defs/small"}],
        "$defs": {"small": {"maximum": 9}}
    })"), applicators());
    CHECK(s.is_valid(json(5)));
    CHECK_FALSE(s.is_valid(json(5.5)));
    CHECK_FALSE(s.is_valid(json(50)));
    const auto errors = errors_of(s, json(50.5));
    REQUIRE(errors.size() == 2);
    CHECK(errors[0].evaluation_path == "/both/0/type");
    CHECK(errors[1].evaluation_path == "/both/1/$ref/maximum");

    // test() reports nothing; the keyword reports its own error.
    const auto e = schema::compile(json::parse(R"({"either": [{"type": "string"}, {"minimum": 10}]})"), applicators());
    CHECK(e.is_valid(json("x")));
    CHECK(e.is_valid(json(12)));
    const auto either_errors = errors_of(e, json(3));
    REQUIRE(either_errors.size() == 1);
    CHECK(either_errors[0].message == "matches neither alternative");

    // An in-place subschema that leads straight back is an infinite recursion.
    CHECK(compile_error(json::parse(R"({"both": [{"$ref": "#"}, true]})"), applicators()).find("infinite recursion") != std::string::npos);
}

TEST_CASE("custom keywords: child subschemas and recursion")
{
    // A tree whose object values are trees: the recursion consumes input, so it is allowed.
    const auto s = schema::compile(json::parse(R"({"type": ["object", "integer"], "eachValue": {"$ref": "#"}})"), applicators());
    CHECK(s.is_valid(json::parse(R"({"a": 1, "b": {"c": 2, "d": {}}})")));
    CHECK_FALSE(s.is_valid(json::parse(R"({"a": 1, "b": {"c": "two"}})")));
    const auto errors = errors_of(s, json::parse(R"({"a": 1, "b": {"c": "two"}})"));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].instance_location.to_string() == "/b/c");
    CHECK(errors[0].evaluation_path == "/eachValue/$ref/eachValue/$ref/type");

    // A child subschema cannot be applied in place, and subschemas must come from the value.
    compile_options bad;
    bad.extra_keywords.push_back({"wrong", {}, [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
                                      static const json outside = json::object();
                                      static_cast<void>(scope.child_subschema(outside));
                                      return nullptr;
                                  }});
    CHECK(compile_error(json::parse(R"({"wrong": {}})"), bad).find("must be part of the keyword's value") != std::string::npos);
    bad.extra_keywords[0].compile = [](const keyword_compile_scope& scope) -> std::unique_ptr<custom_keyword> {
        static_cast<void>(scope.child_subschema(scope.value()));
        return nullptr;
    };
    CHECK(compile_error(json::parse(R"({"wrong": 3})"), bad).find("expected a schema") != std::string::npos);
}

TEST_CASE("custom keywords: vocabularies")
{
    const std::string vocabulary = "https://example.com/vocab/arithmetic";
    compile_options options;
    options.extra_keywords.push_back(multiple_of_all_extension(vocabulary));
    options.resources.push_back({"https://example.com/meta", json::parse(R"({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://example.com/meta",
        "$vocabulary": {"https://json-schema.org/draft/2020-12/vocab/core": true,
                        "https://json-schema.org/draft/2020-12/vocab/validation": true,
                        "https://example.com/vocab/arithmetic": true}
    })")});
    // Not listed by the standard meta-schema: an annotation only.
    CHECK(schema::compile(json::parse(R"({"multipleOfAll": [3]})"), options).is_valid(json(8)));
    // Listed (and required) by the custom meta-schema: applied.
    const auto s = schema::compile(json::parse(R"({"$schema": "https://example.com/meta", "multipleOfAll": [3]})"), options);
    CHECK_FALSE(s.is_valid(json(8)));
    CHECK(s.is_valid(json(9)));
    // Without the registration, the required vocabulary is refused.
    options.extra_keywords.clear();
    CHECK(compile_error(json::parse(R"({"$schema": "https://example.com/meta"})"), options).find("does not support") != std::string::npos);
}

TEST_CASE("custom keywords: standard output")
{
    compile_options options;
    options.extra_keywords.push_back(multiple_of_all_extension());
    const auto s = schema::compile(json::parse(R"({"multipleOfAll": [5]})"), options);
    const json out = s.output(json(7));
    CHECK(out["valid"] == false);
    REQUIRE(out["errors"].size() == 1);
    CHECK(out["errors"][0]["keywordLocation"] == "/multipleOfAll");
}
