// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include "json/pointer.hpp"

namespace jp = jsonschema::detail::json_pointer;
using json = nlohmann::json;

TEST_CASE("json_pointer: escape and append")
{
    CHECK(jp::escape("a/b~c") == "a~1b~0c");
    std::string pointer;
    jp::append(pointer, "$defs");
    jp::append(pointer, "a/b");
    CHECK(pointer == "/$defs/a~1b");
}

TEST_CASE("json_pointer: parse")
{
    CHECK(jp::parse("")->empty());
    CHECK(*jp::parse("/") == std::vector<std::string>{""});
    CHECK(*jp::parse("/a~1b/~0c/") == std::vector<std::string>{"a/b", "~c", ""});
    CHECK_FALSE(jp::parse("a"));
    CHECK_FALSE(jp::parse("/~2"));
    CHECK_FALSE(jp::parse("/~"));
}

TEST_CASE("json_pointer: navigate (RFC 6901 section 5 examples)")
{
    const json doc = json::parse(R"({"foo": ["bar", "baz"], "": 0, "a/b": 1, "c%d": 2, "e^f": 3, "g|h": 4,
                                     "i\\j": 5, "k\"l": 6, " ": 7, "m~n": 8})");
    CHECK(*jp::navigate(doc, "") == doc);
    CHECK(*jp::navigate(doc, "/foo") == json::parse(R"(["bar", "baz"])"));
    CHECK(*jp::navigate(doc, "/foo/0") == "bar");
    CHECK(*jp::navigate(doc, "/") == 0);
    CHECK(*jp::navigate(doc, "/a~1b") == 1);
    CHECK(*jp::navigate(doc, "/c%d") == 2);
    CHECK(*jp::navigate(doc, "/i\\j") == 5);
    CHECK(*jp::navigate(doc, "/k\"l") == 6);
    CHECK(*jp::navigate(doc, "/ ") == 7);
    CHECK(*jp::navigate(doc, "/m~0n") == 8);

    CHECK(jp::navigate(doc, "/missing") == nullptr);
    CHECK(jp::navigate(doc, "/foo/2") == nullptr);
    CHECK(jp::navigate(doc, "/foo/-") == nullptr);
    CHECK(jp::navigate(doc, "/foo/01") == nullptr);
    CHECK(jp::navigate(doc, "/foo/0/x") == nullptr);
    CHECK(jp::navigate(doc, "foo") == nullptr);
}
