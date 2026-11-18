// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Content assertion (design doc 5.5): decoders, media types, contentSchema, and the hooks.
#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>     // doctest prints std::string_view, which MSVC needs the full ostream for
#include <string>
#include <vector>

#include <jsonschema/jsonschema.hpp>

#include "content.hpp"

namespace jd = jsonschema::detail;
using jsonschema::compile_options;
using jsonschema::content_mode;
using jsonschema::schema;
using json = nlohmann::json;

namespace {

schema asserting(const char* text, compile_options options = {})
{
    options.content = content_mode::assert;
    return schema::compile(json::parse(text), std::move(options));
}

std::vector<jsonschema::error> errors_of(const schema& s, const json& instance)
{
    jsonschema::collecting_error_sink sink;
    static_cast<void>(s.validate(instance, {.errors = &sink}));
    return sink.errors;
}

} // namespace

TEST_CASE("content: RFC 4648 decoders")
{
    // RFC 4648 §10 test vectors.
    CHECK(jd::decode_base64("Zm9vYmFy") == "foobar");
    CHECK(jd::decode_base64("Zm9vYg==") == "foob");
    CHECK(jd::decode_base64("") == "");
    CHECK_FALSE(jd::decode_base64("Zm9vYg"));          // padding is required
    CHECK_FALSE(jd::decode_base64("Zm9v YmFy"));
    CHECK_FALSE(jd::decode_base64("Zm9vY==="));
    CHECK_FALSE(jd::decode_base64("Zm9vY-_y"));        // the base64url alphabet
    CHECK(jd::decode_base64url("Zm9vYg") == "foob");   // padding is optional
    CHECK(jd::decode_base64url("-_8") == "\xFB\xFF");
    CHECK(jd::decode_base32("MZXW6YTBOI======") == "foobar");
    CHECK(jd::decode_base32("MZXW6===") == "foo");
    CHECK_FALSE(jd::decode_base32("MZXW6YTBOI"));
    CHECK_FALSE(jd::decode_base32("MZXW6YT1"));
    CHECK(jd::decode_base16("666F6F626172") == "foobar");
    CHECK(jd::decode_base16("666f6f") == "foo");
    CHECK_FALSE(jd::decode_base16("666"));
    CHECK(jd::find_builtin_encoding("BASE64") != nullptr);
    CHECK(jd::find_builtin_encoding("quoted-printable") == nullptr);
    CHECK(jd::normalize_media_type(" Application/JSON; charset=utf-8") == "application/json");
    CHECK(jd::is_json_media_type("application/schema+json"));
    CHECK_FALSE(jd::is_json_media_type("+json"));
    CHECK_FALSE(jd::is_json_media_type("text/plain"));
}

TEST_CASE("content: annotation only by default")
{
    const auto s = schema::compile(json::parse(R"({"contentEncoding": "base64", "contentMediaType": "application/json"})"));
    CHECK(s.is_valid(json("not base64 %%%")));
}

TEST_CASE("content: assertion of encoding and media type")
{
    const auto encoded = asserting(R"({"contentEncoding": "base64"})");
    CHECK(encoded.is_valid(json("eyJmb28iOiAiYmFyIn0K")));
    CHECK_FALSE(encoded.is_valid(json("eyJmb28iOi%iYmFyIn0K")));
    CHECK(encoded.is_valid(json(100)));

    const auto media = asserting(R"({"contentMediaType": "application/json"})");
    CHECK(media.is_valid(json(R"({"foo": "bar"})")));
    CHECK_FALSE(media.is_valid(json("{:}")));

    const auto both = asserting(R"({"contentMediaType": "application/json", "contentEncoding": "base64"})");
    CHECK(both.is_valid(json("eyJmb28iOiAiYmFyIn0K")));
    CHECK_FALSE(both.is_valid(json("ezp9Cg==")));      // valid base64, invalid JSON
    CHECK_FALSE(both.is_valid(json("{}")));            // valid JSON, invalid base64

    // Unknown encodings and media types cannot be checked, so they only annotate.
    CHECK(asserting(R"({"contentEncoding": "quoted-printable", "contentMediaType": "application/json"})").is_valid(json("{:}")));
    CHECK(asserting(R"({"contentMediaType": "image/png"})").is_valid(json("anything")));
    // Media types match ignoring case and parameters, and +json types are JSON.
    CHECK_FALSE(asserting(R"({"contentMediaType": "Application/JSON; charset=utf-8"})").is_valid(json("{:}")));
    CHECK_FALSE(asserting(R"({"contentMediaType": "application/vnd.example+json"})").is_valid(json("{:}")));
}

TEST_CASE("content: contentSchema validates the decoded value")
{
    const auto s = asserting(R"({
        "contentMediaType": "application/json",
        "contentEncoding": "base64",
        "contentSchema": {"type": "object", "required": ["foo"], "properties": {"foo": {"$ref": "#/$defs/text"}}},
        "$defs": {"text": {"type": "string"}}
    })");
    CHECK(s.is_valid(json("eyJmb28iOiAiYmFyIn0K")));         // {"foo": "bar"}
    CHECK_FALSE(s.is_valid(json("eyJib28iOiAyMH0=")));       // {"boo": 20}
    CHECK_FALSE(s.is_valid(json("eyJmb28iOiAyMH0=")));       // {"foo": 20}

    const auto errors = errors_of(s, json("eyJmb28iOiAyMH0="));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].keyword == "contentSchema");
    CHECK(errors[0].instance_location.to_string().empty());
    CHECK(errors[0].message.find("the content does not match contentSchema: ") == 0);
    CHECK(errors[0].schema_location.ends_with("#/contentSchema"));

    // contentSchema without contentMediaType is ignored.
    CHECK(asserting(R"({"contentSchema": false})").is_valid(json("x")));
}

TEST_CASE("content: errors name the keyword that failed")
{
    const auto s = asserting(R"({"contentMediaType": "application/json", "contentEncoding": "base64"})");
    auto errors = errors_of(s, json("%%%%"));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].keyword == "contentEncoding");
    CHECK(errors[0].message == "the string is not valid base64");
    errors = errors_of(s, json("ezp9Cg=="));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].keyword == "contentMediaType");
    CHECK(errors[0].message == "the decoded content is not valid application/json");
    CHECK(errors[0].evaluation_path == "/contentMediaType");
}

TEST_CASE("content: custom decoders and parsers")
{
    compile_options options;
    options.extra_content_encodings.emplace_back("ROT13", [](std::string_view s) -> std::optional<std::string> {
        std::string out(s);
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>('a' + (c - 'a' + 13) % 26);
        }
        return out;
    });
    options.extra_media_types.emplace_back("text/x-number", [](std::string_view s) -> std::optional<json> {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string_view::npos) {
            return std::nullopt;
        }
        return json(std::stoi(std::string(s)));
    });
    const auto s = asserting(R"({"contentEncoding": "rot13", "contentMediaType": "text/x-number", "contentSchema": {"maximum": 10}})",
                             options);
    CHECK(s.is_valid(json("7")));
    CHECK_FALSE(s.is_valid(json("12")));
    CHECK_FALSE(s.is_valid(json("nop")));
}

TEST_CASE("content: still annotations when asserting")
{
    struct collector : jsonschema::annotation_sink
    {
        std::vector<std::string> keywords;
        void on_annotation(const jsonschema::annotation& a) override { keywords.emplace_back(a.keyword); }
    } annotations;
    const auto s = asserting(R"({"contentEncoding": "base64", "contentMediaType": "application/json", "contentSchema": {"type": "object"}})");
    CHECK(s.validate(json("e30="), {.annotations = &annotations}).valid);   // {}
    std::sort(annotations.keywords.begin(), annotations.keywords.end());
    CHECK(annotations.keywords == std::vector<std::string>{"contentEncoding", "contentMediaType", "contentSchema"});
}
