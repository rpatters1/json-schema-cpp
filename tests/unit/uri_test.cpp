// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include <jsonschema/uri.hpp>

using jsonschema::uri;

namespace {

std::string resolve(const char* base, const char* reference)
{
    return uri(base).resolve(uri(reference)).to_string();
}

std::string normalize(const char* text)
{
    return uri(text).normalized().to_string();
}

} // namespace

TEST_CASE("uri: parse components")
{
    const uri u("foo://user:pw@example.com:8042/over/there?name=ferret#nose");
    REQUIRE(u.scheme());
    CHECK(*u.scheme() == "foo");
    REQUIRE(u.authority());
    CHECK(*u.authority() == "user:pw@example.com:8042");
    REQUIRE(u.userinfo());
    CHECK(*u.userinfo() == "user:pw");
    CHECK(u.host() == "example.com");
    REQUIRE(u.port());
    CHECK(*u.port() == "8042");
    CHECK(u.path() == "/over/there");
    REQUIRE(u.query());
    CHECK(*u.query() == "name=ferret");
    REQUIRE(u.fragment());
    CHECK(*u.fragment() == "nose");
    CHECK(u.has_scheme());
    CHECK_FALSE(u.is_absolute());
    CHECK(u.without_fragment().is_absolute());
}

TEST_CASE("uri: absent vs empty components round-trip")
{
    for (const char* text : {"", "a", "a?", "a#", "a?#", "?", "#", "//", "//host", "s:", "s://", "s:?#",
                             "urn:uuid:deadbeef-1234-ffff-ffff-4321feebdaed", "#/$defs/foo%25bar"}) {
        CAPTURE(text);
        CHECK(uri(text).to_string() == text);
    }
    CHECK_FALSE(uri("a").query());
    REQUIRE(uri("a?").query());
    CHECK(uri("a?").query()->empty());
    CHECK_FALSE(uri("a").fragment());
    REQUIRE(uri("a#").fragment());
}

TEST_CASE("uri: URNs and JSON Schema style references")
{
    const uri urn("urn:uuid:deadbeef-1234-ffff-ffff-4321feebdaed");
    CHECK(urn.is_absolute());
    CHECK_FALSE(urn.authority());
    CHECK(urn.path() == "uuid:deadbeef-1234-ffff-ffff-4321feebdaed");
    CHECK(urn.resolve(uri("#foo")).to_string() == "urn:uuid:deadbeef-1234-ffff-ffff-4321feebdaed#foo");
    CHECK(urn.resolve(uri("#/$defs/bar")).to_string() == "urn:uuid:deadbeef-1234-ffff-ffff-4321feebdaed#/$defs/bar");

    CHECK(resolve("http://localhost:1234/draft2020-12/tree", "node") == "http://localhost:1234/draft2020-12/node");
    CHECK(resolve("http://example.com/root.json", "#/$defs/A") == "http://example.com/root.json#/$defs/A");
    CHECK(resolve("http://example.com/a/b.json", "/c.json") == "http://example.com/c.json");
    CHECK(resolve("http://example.com/a/b.json", "https://other.example/x") == "https://other.example/x");
    CHECK(resolve("file:///folder/file.json", "#foo") == "file:///folder/file.json#foo");
    CHECK(resolve("file:///folder/file.json", "other.json") == "file:///folder/other.json");
}

TEST_CASE("uri: reject invalid references")
{
    for (const char* text : {
             "http://exa mple.com/",   // space
             "http://example.com/%",   // truncated pct-encoding
             "http://example.com/%zz", // bad pct-encoding
             "a:b c",                  // space in path
             "1a:b",                   // relative-path with ':' in first segment
             ":foo",                   // relative-path with ':' in first segment
             "http://[::1/",           // unterminated IP literal
             "http://[1:2:3]/",        // invalid IPv6
             "http://host:80a/",       // non-digit port
             "http://a@b@c/",          // '@' in host
             "#frag#ment",             // '#' in fragment
             "http://example.com/\\",  // backslash
             "\"quoted\"",             // quote
             "http://example.com/{x}", // braces
         }) {
        CAPTURE(text);
        CHECK_FALSE(uri::parse(text));
    }
    CHECK_THROWS_AS(uri("http://exa mple.com/"), jsonschema::uri_error);
}

TEST_CASE("uri: accept valid edge cases")
{
    for (const char* text : {
             "http://[::1]:8080/",
             "http://[2001:db8::7]/c=GB?objectClass?one",
             "http://[v7.fe80::abcd]/",
             "http://[::ffff:192.0.2.128]/",
             "ldap://[2001:db8::7]/c=GB?objectClass?one",
             "mailto:John.Doe@example.com",
             "news:comp.infosystems.www.servers.unix",
             "tel:+1-816-555-1212",
             "telnet://192.0.2.16:80/",
             "urn:oasis:names:specification:docbook:dtd:xml:4.1.2",
             "http://host:/",
             "./a:b",
             "a/b:c",
             "//example.com",
             "?query/with?slashes",
             "#/$defs/a~1b",
             "#%25",
         }) {
        CAPTURE(text);
        CHECK(uri::parse(text));
    }
}

TEST_CASE("uri: RFC 3986 section 5.4.1 normal examples")
{
    const char* base = "http://a/b/c/d;p?q";
    const std::vector<std::pair<const char*, const char*>> cases = {
        {"g:h", "g:h"},
        {"g", "http://a/b/c/g"},
        {"./g", "http://a/b/c/g"},
        {"g/", "http://a/b/c/g/"},
        {"/g", "http://a/g"},
        {"//g", "http://g"},
        {"?y", "http://a/b/c/d;p?y"},
        {"g?y", "http://a/b/c/g?y"},
        {"#s", "http://a/b/c/d;p?q#s"},
        {"g#s", "http://a/b/c/g#s"},
        {"g?y#s", "http://a/b/c/g?y#s"},
        {";x", "http://a/b/c/;x"},
        {"g;x", "http://a/b/c/g;x"},
        {"g;x?y#s", "http://a/b/c/g;x?y#s"},
        {"", "http://a/b/c/d;p?q"},
        {".", "http://a/b/c/"},
        {"./", "http://a/b/c/"},
        {"..", "http://a/b/"},
        {"../", "http://a/b/"},
        {"../g", "http://a/b/g"},
        {"../..", "http://a/"},
        {"../../", "http://a/"},
        {"../../g", "http://a/g"},
    };
    for (const auto& [reference, expected] : cases) {
        CAPTURE(reference);
        CHECK(resolve(base, reference) == expected);
    }
}

TEST_CASE("uri: RFC 3986 section 5.4.2 abnormal examples")
{
    const char* base = "http://a/b/c/d;p?q";
    const std::vector<std::pair<const char*, const char*>> cases = {
        {"../../../g", "http://a/g"},
        {"../../../../g", "http://a/g"},
        {"/./g", "http://a/g"},
        {"/../g", "http://a/g"},
        {"g.", "http://a/b/c/g."},
        {".g", "http://a/b/c/.g"},
        {"g..", "http://a/b/c/g.."},
        {"..g", "http://a/b/c/..g"},
        {"./../g", "http://a/b/g"},
        {"./g/.", "http://a/b/c/g/"},
        {"g/./h", "http://a/b/c/g/h"},
        {"g/../h", "http://a/b/c/h"},
        {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
        {"g;x=1/../y", "http://a/b/c/y"},
        {"g?y/./x", "http://a/b/c/g?y/./x"},
        {"g?y/../x", "http://a/b/c/g?y/../x"},
        {"g#s/./x", "http://a/b/c/g#s/./x"},
        {"g#s/../x", "http://a/b/c/g#s/../x"},
        {"http:g", "http:g"}, // strict parser
    };
    for (const auto& [reference, expected] : cases) {
        CAPTURE(reference);
        CHECK(resolve(base, reference) == expected);
    }
}

TEST_CASE("uri: remove_dot_segments (RFC 3986 section 5.2.4 examples)")
{
    CHECK(jsonschema::remove_dot_segments("/a/b/c/./../../g") == "/a/g");
    CHECK(jsonschema::remove_dot_segments("mid/content=5/../6") == "mid/6");
    CHECK(jsonschema::remove_dot_segments("") == "");
    CHECK(jsonschema::remove_dot_segments("/") == "/");
    CHECK(jsonschema::remove_dot_segments("/..") == "/");
    CHECK(jsonschema::remove_dot_segments("a/..") == "/");
}

TEST_CASE("uri: normalization (RFC 3986 section 6.2.2 and 6.2.3)")
{
    CHECK(normalize("HTTP://www.Example.com/") == "http://www.example.com/");
    CHECK(normalize("http://example.com/%7euser") == "http://example.com/~user");
    CHECK(normalize("http://example.com/a%2fb") == "http://example.com/a%2Fb");
    CHECK(normalize("http://example.com/%41") == "http://example.com/A");
    CHECK(normalize("http://%45XAMPLE.com/") == "http://example.com/");
    CHECK(normalize("http://example.com") == "http://example.com/");
    CHECK(normalize("http://example.com:80/") == "http://example.com/");
    CHECK(normalize("https://example.com:443/x") == "https://example.com/x");
    CHECK(normalize("http://example.com:8080/") == "http://example.com:8080/");
    CHECK(normalize("foo://example.com:/") == "foo://example.com/");
    CHECK(normalize("http://example.com/a/./b/../c") == "http://example.com/a/c");
    CHECK(normalize("http://example.com/#/$defs/%7e") == "http://example.com/#/$defs/~");
    CHECK(normalize("urn:UUID:ABC") == "urn:UUID:ABC"); // path is case-sensitive
    CHECK(normalize("URN:uuid:abc") == "urn:uuid:abc");
    CHECK(normalize("a/./b") == "a/./b");               // relative references are left alone

    CHECK(uri("HTTP://Example.com").normalized() == uri("http://example.com/").normalized());
}

TEST_CASE("uri: with_fragment / without_fragment")
{
    const uri u("http://example.com/s.json#/$defs/a");
    CHECK(u.without_fragment().to_string() == "http://example.com/s.json");
    CHECK(u.with_fragment("x").to_string() == "http://example.com/s.json#x");
    CHECK(u.with_fragment("").to_string() == "http://example.com/s.json#");
}

TEST_CASE("uri: percent_decode")
{
    CHECK(jsonschema::percent_decode("a%25b%22c") == std::optional<std::string>("a%b\"c"));
    CHECK(jsonschema::percent_decode("%e2%82%AC") == std::optional<std::string>("\xE2\x82\xAC"));
    CHECK_FALSE(jsonschema::percent_decode("%"));
    CHECK_FALSE(jsonschema::percent_decode("%4"));
    CHECK_FALSE(jsonschema::percent_decode("%4g"));
}

TEST_CASE("uri: IPv4 and IPv6 address syntax")
{
    using jsonschema::is_ipv4_address;
    using jsonschema::is_ipv6_address;

    CHECK(is_ipv4_address("0.0.0.0"));
    CHECK(is_ipv4_address("192.168.0.1"));
    CHECK(is_ipv4_address("255.255.255.255"));
    CHECK_FALSE(is_ipv4_address("256.0.0.1"));
    CHECK_FALSE(is_ipv4_address("01.2.3.4"));
    CHECK_FALSE(is_ipv4_address("1.2.3"));
    CHECK_FALSE(is_ipv4_address("1.2.3.4.5"));
    CHECK_FALSE(is_ipv4_address("1..3.4"));
    CHECK_FALSE(is_ipv4_address("a.b.c.d"));

    for (const char* text : {"::", "::1", "1::", "1:2:3:4:5:6:7:8", "1::8", "1:2:3:4:5:6::8", "fe80::1",
                             "::ffff:192.0.2.128", "1:2:3:4:5:6:1.2.3.4", "2001:DB8::7"}) {
        CAPTURE(text);
        CHECK(is_ipv6_address(text));
    }
    for (const char* text : {"", ":", ":::", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "1::2::3", "12345::",
                             ":1::2", "1::2:", "g::1", "1:2:3:4:5:6:7:1.2.3.4", "::1.2.3", "1.2.3.4::"}) {
        CAPTURE(text);
        CHECK_FALSE(is_ipv6_address(text));
    }
}
