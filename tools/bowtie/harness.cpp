// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// A Bowtie harness for json-schema-cpp (https://docs.bowtie.report/en/stable/implementers/).
//
// Speaks Bowtie's IHOP protocol, version 1: one JSON request per line on standard input,
// one JSON response per line on standard output. Commands: start, dialect, run, stop.
// Both output modes are supported: "flag", and "annotations" (from the annotation sink).

#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include <jsonschema/jsonschema.hpp>

using json = nlohmann::json;

namespace {

constexpr int protocol_version = 1;
const char* const dialect_2020_12 = "https://json-schema.org/draft/2020-12/schema";

std::string compiler_version()
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

json start_response()
{
    return {
        {"version", protocol_version},
        {"implementation",
         {
             {"language", "c++"},
             {"name", "json-schema-cpp"},
             {"version", std::string(jsonschema::version())},
             {"homepage", "https://github.com/rpatters1/json-schema-cpp"},
             {"issues", "https://github.com/rpatters1/json-schema-cpp/issues"},
             {"source", "https://github.com/rpatters1/json-schema-cpp"},
             {"documentation", "https://github.com/rpatters1/json-schema-cpp#readme"},
             {"dialects", json::array({dialect_2020_12})},
             {"language_version", compiler_version()},
         }},
    };
}

struct annotation_collector final : jsonschema::annotation_sink
{
    json items = json::array();
    void on_annotation(const jsonschema::annotation& a) override
    {
        items.push_back({
            {"keyword", std::string(a.keyword)},
            {"instanceLocation", a.instance_location.to_string()},
            {"absoluteKeywordLocation", a.schema_location},
            {"annotation", a.value},
        });
    }
};

json errored(const std::string& message)
{
    return {{"errored", true}, {"context", {{"message", message}}}};
}

json run(const json& request)
{
    const json& test_case = request.at("case");
    const bool annotations = request.value("output", std::string("flag")) == "annotations";

    jsonschema::compile_options options;
    if (const auto registry = test_case.find("registry"); registry != test_case.end() && registry->is_object()) {
        for (const auto& [uri, document] : registry->items()) {
            options.resources.push_back({uri, document});
        }
    }

    json response = {{"seq", request.at("seq")}};
    std::optional<jsonschema::schema> compiled;
    try {
        compiled.emplace(jsonschema::schema::compile(test_case.at("schema"), std::move(options)));
    } catch (const std::exception& e) {
        response.update(errored(e.what()));
        return response;
    }

    json results = json::array();
    for (const auto& test : test_case.at("tests")) {
        try {
            if (annotations) {
                annotation_collector collector;
                const bool valid = compiled->validate(test.at("instance"), {.annotations = &collector}).valid;
                results.push_back({{"valid", valid}, {"annotations", std::move(collector.items)}});
            } else {
                results.push_back({{"valid", compiled->is_valid(test.at("instance"))}});
            }
        } catch (const std::exception& e) {
            results.push_back(errored(e.what()));
        }
    }
    response["results"] = std::move(results);
    return response;
}

void reply(const json& response)
{
    std::cout << response.dump() << std::endl;   // endl flushes: Bowtie reads line by line
}

} // namespace

int main()
{
    std::string line;
    bool started = false;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        json request;
        try {
            request = json::parse(line);
        } catch (const json::parse_error& e) {
            std::cerr << "json-schema-cpp bowtie harness: invalid request: " << e.what() << "\n";
            return 1;
        }
        const std::string command = request.value("cmd", std::string());
        if (command == "start") {
            if (request.value("version", 0) != protocol_version) {
                std::cerr << "json-schema-cpp bowtie harness: unsupported protocol version\n";
                return 1;
            }
            started = true;
            reply(start_response());
        } else if (!started) {
            std::cerr << "json-schema-cpp bowtie harness: \"" << command << "\" before \"start\"\n";
            return 1;
        } else if (command == "dialect") {
            // Schemas without $schema are 2020-12, the only dialect offered.
            reply({{"ok", request.value("dialect", std::string()) == dialect_2020_12}});
        } else if (command == "run") {
            try {
                reply(run(request));
            } catch (const std::exception& e) {
                // A malformed request: still answer, so Bowtie need not restart the container.
                json response = errored(e.what());
                response["seq"] = request.value("seq", json());
                reply(response);
            }
        } else if (command == "stop") {
            return 0;
        } else {
            std::cerr << "json-schema-cpp bowtie harness: unknown command \"" << command << "\"\n";
            return 1;
        }
    }
    return 0;
}
