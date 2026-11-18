// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Runs the JSON-Schema-Test-Suite annotation tests (annotations/tests/*.json) for 2020-12.
//
// Usage: jsonschema_annotation_runner --tests-dir DIR [--verbose] [FILE...]
//
// Each assertion names an instance location, a keyword, and the annotations expected
// there, keyed by schema location. The suite writes schema locations as pointers from the
// document root ("#/properties/foo"), even inside embedded resources. The library reports
// canonical absolute locations ("https://example.com/inner#/properties/foo"), so this runner
// converts each expected key to the canonical form by resolving any `$id` along its path.
//
// Exit status: 0 if every assertion held, 1 otherwise, 2 on usage errors.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <jsonschema/jsonschema.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int dialect_under_test = 2020;
const std::string root_uri = "https://json-schema-cpp.test/annotations/root.json";

bool is_compatible(std::string_view compatibility, int version)
{
    bool valid = true;
    std::size_t start = 0;
    while (start <= compatibility.size()) {
        const std::size_t comma = compatibility.find(',', start);
        std::string_view constraint = compatibility.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!constraint.empty() && constraint.front() == ' ') constraint.remove_prefix(1);
        std::string_view op;
        if (constraint.starts_with("<=")) {
            op = "<=";
        } else if (constraint.starts_with("=")) {
            op = "=";
        }
        const int v = std::stoi(std::string(constraint.substr(op.size())));
        valid = op.empty() ? version >= v : (op == "<=" ? version <= v : version == v);
        if (version <= v || comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return valid;
}

/// A schema location in comparable form: resource URI (normalized) + decoded pointer.
struct location_key
{
    std::string resource;
    std::string pointer;
    auto operator<=>(const location_key&) const = default;
};

std::string unescape_token(const std::string& token)
{
    std::string out;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '~' && i + 1 < token.size()) {
            out += token[i + 1] == '1' ? '/' : '~';
            ++i;
        } else {
            out += token[i];
        }
    }
    return out;
}

std::vector<std::string> split_pointer(const std::string& pointer)
{
    std::vector<std::string> tokens;
    if (pointer.empty()) {
        return tokens;
    }
    std::size_t start = 1;
    while (true) {
        const std::size_t slash = pointer.find('/', start);
        tokens.push_back(pointer.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return tokens;
}

/// The library's form of an absolute location, minus a trailing keyword if @p strip_keyword.
std::optional<location_key> parse_location(const std::string& text, bool strip_keyword)
{
    const auto u = jsonschema::uri::parse(text);
    if (!u) {
        return std::nullopt;
    }
    auto pointer = jsonschema::percent_decode(u->fragment().value_or(""));
    if (!pointer) {
        return std::nullopt;
    }
    if (strip_keyword) {
        const auto slash = pointer->rfind('/');
        pointer->resize(slash == std::string::npos ? 0 : slash);
    }
    return location_key{u->without_fragment().normalized().to_string(), *pointer};
}

/// Converts the suite's document-relative key ("#/a/b") to the canonical location.
std::optional<location_key> canonical(const json& schema, const jsonschema::uri& root_base, const std::string& key)
{
    const auto u = jsonschema::uri::parse(key);
    if (!u) {
        return std::nullopt;
    }
    const auto pointer = jsonschema::percent_decode(u->fragment().value_or(""));
    if (!pointer) {
        return std::nullopt;
    }
    jsonschema::uri base = root_base;
    std::string in_resource;
    const json* current = &schema;
    for (const auto& raw : split_pointer(*pointer)) {
        const std::string token = unescape_token(raw);
        if (current->is_object() && current->contains(token)) {
            current = &(*current)[token];
        } else if (current->is_array()) {
            current = &(*current)[static_cast<std::size_t>(std::stoul(token))];
        } else {
            return std::nullopt;
        }
        in_resource += "/" + raw;
        if (current->is_object()) {
            if (const auto id = current->find("$id"); id != current->end() && id->is_string()) {
                base = base.resolve(jsonschema::uri(id->get<std::string>())).without_fragment();
                in_resource.clear();
            }
        }
    }
    // Decode tokens the same way the library's pointers are written.
    std::string decoded;
    for (const auto& raw : split_pointer(in_resource)) {
        decoded += "/" + raw;
    }
    return location_key{base.normalized().to_string(), decoded};
}

struct collected : jsonschema::annotation_sink
{
    struct item
    {
        std::string instance_location;
        std::string keyword;
        std::string schema_location;
        json value;
    };
    std::vector<item> items;
    void on_annotation(const jsonschema::annotation& a) override
    {
        items.push_back({a.instance_location.to_string(), std::string(a.keyword), a.schema_location, a.value});
    }
};

struct totals
{
    std::size_t tests = 0;
    std::size_t passed = 0;
    std::vector<std::string> problems;
};

void run_file(const fs::path& path, const std::string& name, totals& t)
{
    std::ifstream in(path);
    const json document = json::parse(in);
    for (const auto& test_case : document.at("suite")) {
        if (const auto c = test_case.find("compatibility"); c != test_case.end() && !is_compatible(c->get<std::string>(), dialect_under_test)) {
            continue;
        }
        const std::string case_description = test_case.at("description").get<std::string>();
        jsonschema::compile_options options;
        options.base_uri = root_uri;
        if (const auto externals = test_case.find("externalSchemas"); externals != test_case.end()) {
            for (const auto& [u, s] : externals->items()) {
                options.resources.push_back({u, s});
            }
        }
        std::optional<jsonschema::schema> compiled;
        std::string compile_error;
        try {
            compiled.emplace(jsonschema::schema::compile(test_case.at("schema"), options));
        } catch (const std::exception& e) {
            compile_error = e.what();
        }
        std::size_t index = 0;
        for (const auto& test : test_case.at("tests")) {
            ++t.tests;
            const std::string label = name + " [" + case_description + "] #" + std::to_string(index++);
            if (!compiled) {
                t.problems.push_back(label + ": schema did not compile: " + compile_error);
                continue;
            }
            collected sink;
            static_cast<void>(compiled->validate(test.at("instance"), {.annotations = &sink}));
            std::vector<std::string> failures;
            for (const auto& assertion : test.at("assertions")) {
                const std::string location = assertion.at("location").get<std::string>();
                const std::string keyword = assertion.at("keyword").get<std::string>();
                std::map<location_key, json> actual;
                for (const auto& a : sink.items) {
                    if (a.instance_location == location && a.keyword == keyword) {
                        if (const auto k = parse_location(a.schema_location, true)) {
                            actual.emplace(*k, a.value);
                        }
                    }
                }
                std::map<location_key, json> expected;
                for (const auto& [key, value] : assertion.at("expected").items()) {
                    const auto k = canonical(test_case.at("schema"), compiled->base_uri(), key);
                    if (!k) {
                        failures.push_back("cannot interpret expected key " + key);
                        continue;
                    }
                    expected.emplace(*k, value);
                }
                if (actual != expected) {
                    std::string text = "at \"" + location + "\" for " + keyword + ": expected {";
                    for (const auto& [k, v] : expected) text += " " + k.resource + "#" + k.pointer + "=" + v.dump();
                    text += " }, got {";
                    for (const auto& [k, v] : actual) text += " " + k.resource + "#" + k.pointer + "=" + v.dump();
                    failures.push_back(text + " }");
                }
            }
            if (failures.empty()) {
                ++t.passed;
            } else {
                for (const auto& f : failures) {
                    t.problems.push_back(label + ": " + f);
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    fs::path tests_dir;
    bool verbose = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--tests-dir" && i + 1 < argc) {
            tests_dir = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg.starts_with("--")) {
            std::cerr << "unknown option " << arg << "\n";
            return 2;
        } else {
            files.emplace_back(arg);
        }
    }
    if (tests_dir.empty()) {
        std::cerr << "usage: " << argv[0] << " --tests-dir DIR [--verbose] [FILE...]\n";
        return 2;
    }
    if (files.empty()) {
        for (const auto& entry : fs::directory_iterator(tests_dir)) {
            if (entry.path().extension() == ".json") {
                files.push_back(entry.path().filename().string());
            }
        }
        std::sort(files.begin(), files.end());
    }
    totals t;
    for (const auto& f : files) {
        try {
            run_file(tests_dir / f, f, t);
        } catch (const std::exception& e) {
            t.problems.push_back(f + ": runner error: " + e.what());
        }
    }
    std::cout << "annotation suite: " << t.passed << " / " << t.tests << " tests passed\n";
    const std::size_t limit = verbose ? t.problems.size() : std::min<std::size_t>(t.problems.size(), 25);
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  " << t.problems[i] << "\n";
    }
    return t.problems.empty() ? 0 : 1;
}
