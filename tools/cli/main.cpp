// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// json-schema-cpp: validate JSON documents against a JSON Schema 2020-12 schema.
//
//   json-schema-cpp [options] SCHEMA [INSTANCE...]
//
// Each INSTANCE is a file, or "-" for standard input (the default when none is given).
//
// Options:
//   --output text|flag|basic|detailed   output format (default: text)
//   --resource URI=FILE                  register FILE as the schema with URI (repeatable)
//   --base-uri URI                       base URI of SCHEMA (default: its file: URI)
//   --max-errors N                       report at most N errors per instance (text output)
//   --assert-formats                     make `format` an assertion
//   --assert-content                     check contentEncoding, contentMediaType, contentSchema
//   --no-metaschema                      do not validate the schema against its meta-schema
//   --strict-dependencies                ignore the superseded `dependencies` keyword
//   --quiet                              print nothing; use the exit status only
//   --version                            print the version and exit
//
// Referenced schemas with file: URIs are read from disk, so a schema can $ref files next
// to it. Nothing is fetched over the network.
//
// Exit status: 0 if every instance is valid, 1 if any is invalid, 2 on usage errors,
// unreadable files, invalid JSON or an invalid schema.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <jsonschema/jsonschema.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int exit_valid = 0;
constexpr int exit_invalid = 1;
constexpr int exit_error = 2;

void usage(std::ostream& out)
{
    out << "usage: json-schema-cpp [options] SCHEMA [INSTANCE...]\n"
           "\n"
           "Validates each INSTANCE (a file, or - for standard input) against SCHEMA (JSON Schema 2020-12).\n"
           "\n"
           "options:\n"
           "  --output text|flag|basic|detailed   output format (default: text)\n"
           "  --resource URI=FILE                  register FILE as the schema with URI (repeatable)\n"
           "  --base-uri URI                       base URI of SCHEMA (default: its file: URI)\n"
           "  --max-errors N                       report at most N errors per instance (text output)\n"
           "  --assert-formats                     make `format` an assertion\n"
           "  --assert-content                     check contentEncoding, contentMediaType, contentSchema\n"
           "  --no-metaschema                      do not validate SCHEMA against its meta-schema\n"
           "  --strict-dependencies                ignore the superseded `dependencies` keyword\n"
           "  --quiet                              print nothing; use the exit status only\n"
           "  --version                            print the version and exit\n"
           "\n"
           "exit status: 0 all valid, 1 some invalid, 2 error\n";
}

/// A file: URI for @p path (absolute, percent-encoded).
std::string file_uri(const fs::path& path)
{
    std::string generic = fs::absolute(path).lexically_normal().generic_string();
    if (generic.empty() || generic.front() != '/') {
        generic.insert(generic.begin(), '/');   // Windows: "/C:/..."
    }
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (const char c : generic) {
        const auto u = static_cast<unsigned char>(c);
        const bool plain = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
                           std::string_view("-._~/:!$&'()*+,;=@").find(c) != std::string_view::npos;
        if (plain) {
            uri += c;
        } else {
            uri += '%';
            uri += hex[u >> 4];
            uri += hex[u & 0xF];
        }
    }
    return uri;
}

/// The local path of a file: URI, or nullopt for anything else.
std::optional<fs::path> local_path(const jsonschema::uri& u)
{
    if (!u.scheme() || *u.scheme() != "file" || (u.authority() && !u.authority()->empty() && *u.authority() != "localhost")) {
        return std::nullopt;
    }
    auto decoded = jsonschema::percent_decode(u.path());
    if (!decoded) {
        return std::nullopt;
    }
    std::string path = *decoded;
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
        path.erase(0, 1);   // "/C:/x" on Windows
    }
    return fs::path(path);
}

std::optional<json> read_json(std::istream& in, const std::string& name, std::string& problem)
{
    try {
        return json::parse(in);
    } catch (const json::parse_error& e) {
        problem = name + ": invalid JSON: " + e.what();
        return std::nullopt;
    }
}

std::optional<json> read_json_file(const fs::path& path, std::string& problem)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        problem = path.string() + ": cannot open";
        return std::nullopt;
    }
    return read_json(in, path.string(), problem);
}

enum class format
{
    text,
    flag,
    basic,
    detailed
};

} // namespace

int main(int argc, char** argv)
{
    format output = format::text;
    std::vector<std::pair<std::string, std::string>> resources;
    std::optional<std::string> base_uri;
    std::size_t max_errors = 0;
    bool check_metaschema = true;
    bool assert_formats = false;
    bool assert_content = false;
    bool legacy_dependencies = true;
    bool quiet = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "json-schema-cpp: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };
        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return exit_valid;
        } else if (arg == "--version") {
            std::cout << "json-schema-cpp " << jsonschema::version() << "\n";
            return exit_valid;
        } else if (arg == "--output") {
            const auto v = value();
            if (!v) return exit_error;
            if (*v == "text") output = format::text;
            else if (*v == "flag") output = format::flag;
            else if (*v == "basic") output = format::basic;
            else if (*v == "detailed") output = format::detailed;
            else {
                std::cerr << "json-schema-cpp: unknown output format " << *v << "\n";
                return exit_error;
            }
        } else if (arg == "--resource") {
            const auto v = value();
            if (!v) return exit_error;
            const auto eq = v->find('=');
            if (eq == std::string::npos) {
                std::cerr << "json-schema-cpp: --resource needs URI=FILE\n";
                return exit_error;
            }
            resources.emplace_back(v->substr(0, eq), v->substr(eq + 1));
        } else if (arg == "--base-uri") {
            base_uri = value();
            if (!base_uri) return exit_error;
        } else if (arg == "--max-errors") {
            const auto v = value();
            if (!v) return exit_error;
            max_errors = static_cast<std::size_t>(std::strtoull(v->c_str(), nullptr, 10));
        } else if (arg == "--assert-formats") {
            assert_formats = true;
        } else if (arg == "--assert-content") {
            assert_content = true;
        } else if (arg == "--no-metaschema") {
            check_metaschema = false;
        } else if (arg == "--strict-dependencies") {
            legacy_dependencies = false;
        } else if (arg == "--quiet" || arg == "-q") {
            quiet = true;
        } else if (arg.size() > 1 && arg.front() == '-') {
            std::cerr << "json-schema-cpp: unknown option " << arg << "\n";
            usage(std::cerr);
            return exit_error;
        } else {
            positional.emplace_back(arg);
        }
    }
    if (positional.empty()) {
        usage(std::cerr);
        return exit_error;
    }

    std::string problem;
    const fs::path schema_path = positional.front();
    auto schema_json = read_json_file(schema_path, problem);
    if (!schema_json) {
        std::cerr << "json-schema-cpp: " << problem << "\n";
        return exit_error;
    }

    jsonschema::compile_options options;
    options.base_uri = base_uri ? *base_uri : file_uri(schema_path);
    options.validate_against_metaschema = check_metaschema;
    options.legacy_dependencies = legacy_dependencies;
    if (assert_formats) {
        options.formats = jsonschema::format_mode::assert;
    }
    if (assert_content) {
        options.content = jsonschema::content_mode::assert;
    }
    for (const auto& [uri, file] : resources) {
        auto document = read_json_file(file, problem);
        if (!document) {
            std::cerr << "json-schema-cpp: " << problem << "\n";
            return exit_error;
        }
        options.resources.push_back({uri, std::move(*document)});
    }
    options.loader = [](const jsonschema::uri& u) -> std::optional<json> {
        const auto path = local_path(u);
        if (!path) {
            return std::nullopt;
        }
        std::string why;
        auto document = read_json_file(*path, why);
        if (!document) {
            throw std::runtime_error(why);
        }
        return document;
    };

    std::optional<jsonschema::schema> compiled;
    try {
        compiled.emplace(jsonschema::schema::compile(std::move(*schema_json), std::move(options)));
    } catch (const jsonschema::schema_error& e) {
        std::cerr << schema_path.string() << ": " << e.what() << "\n";
        return exit_error;
    }

    std::vector<std::string> instances(positional.begin() + 1, positional.end());
    if (instances.empty()) {
        instances.emplace_back("-");
    }

    int status = exit_valid;
    for (const auto& argument : instances) {
        const std::string name = argument == "-" ? std::string("<stdin>") : argument;
        std::optional<json> instance;
        if (argument == "-") {
            instance = read_json(std::cin, name, problem);
        } else {
            instance = read_json_file(argument, problem);
        }
        if (!instance) {
            std::cerr << "json-schema-cpp: " << problem << "\n";
            status = exit_error;
            continue;
        }
        try {
            bool valid = false;
            if (output == format::text) {
                jsonschema::collecting_error_sink errors;
                valid = compiled->validate(*instance, {.errors = &errors, .max_errors = max_errors}).valid;
                if (!quiet) {
                    if (valid) {
                        std::cout << name << ": valid\n";
                    } else {
                        std::cout << name << ": invalid\n";
                        for (const auto& e : errors.errors) {
                            const std::string where = e.instance_location.empty() ? std::string("(root)") : e.instance_location.to_string();
                            std::cout << "  " << where << ": " << e.message << "  [" << e.evaluation_path << "]\n";
                        }
                    }
                }
            } else {
                const auto standard = output == format::flag    ? jsonschema::output_format::flag
                                      : output == format::basic ? jsonschema::output_format::basic
                                                                : jsonschema::output_format::detailed;
                const json result = compiled->output(*instance, standard);
                valid = result.at("valid").get<bool>();
                if (!quiet) {
                    std::cout << result.dump(2) << "\n";
                }
            }
            if (!valid && status == exit_valid) {
                status = exit_invalid;
            }
        } catch (const jsonschema::evaluation_error& e) {
            std::cerr << name << ": " << e.what() << "\n";
            status = exit_error;
        }
    }
    return status;
}
