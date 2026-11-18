// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// Runs JSON-Schema-Test-Suite validation tests (validation/tests/**.json) against
// json-schema-cpp for the 2020-12 dialect.
//
// Usage:
//   jsonschema_suite_runner --tests-dir DIR [--expected-failures FILE]
//                           [--verbose] [--markdown] [FILE...]
//
// FILE paths are relative to --tests-dir (e.g. "ref.json", "optional/bignum.json").
// With no FILE arguments, every file in DIR and DIR/optional is run.
//
// Exit status: 0 if every result matched expectations, 1 if any did not,
// 77 if none of the named files contains a test case that applies to 2020-12, 2 on usage errors.

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <jsonschema/jsonschema.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int dialect_under_test = 2020;
constexpr int exit_ok = 0;
constexpr int exit_failed = 1;
constexpr int exit_usage = 2;
constexpr int exit_skipped = 77;
constexpr std::size_t default_failure_listing_limit = 25;

/// The suite's compatibility algorithm (validation/README.md, "Compatibility matching").
bool is_compatible(std::string_view compatibility, int version)
{
    bool valid = true;
    std::size_t start = 0;
    while (start <= compatibility.size()) {
        const std::size_t comma = compatibility.find(',', start);
        std::string_view constraint = compatibility.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!constraint.empty() && constraint.front() == ' ') constraint.remove_prefix(1);
        while (!constraint.empty() && constraint.back() == ' ') constraint.remove_suffix(1);

        std::string_view op;
        if (constraint.starts_with("<=")) {
            op = "<=";
        } else if (constraint.starts_with("=")) {
            op = "=";
        }
        const int constraint_version = std::stoi(std::string(constraint.substr(op.size())));
        if (op.empty()) {
            valid = version >= constraint_version;
        } else if (op == "<=") {
            valid = version <= constraint_version;
        } else {
            valid = version == constraint_version;
        }
        if (version <= constraint_version || comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return valid;
}

struct expected_failure
{
    std::string file;
    std::optional<std::string> test_case;   // nullopt: every case in the file
    std::optional<std::string> test;        // nullopt: every test in the case
    std::string reason;
    mutable bool used = false;
};

std::vector<expected_failure> load_expected_failures(const fs::path& path)
{
    std::vector<expected_failure> result;
    if (path.empty()) {
        return result;
    }
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open expected-failures file " + path.string());
    }
    const json document = json::parse(in);
    for (const auto& entry : document.at("entries")) {
        expected_failure item;
        item.file = entry.at("file").get<std::string>();
        if (entry.contains("case")) item.test_case = entry["case"].get<std::string>();
        if (entry.contains("test")) item.test = entry["test"].get<std::string>();
        item.reason = entry.at("reason").get<std::string>();
        result.push_back(std::move(item));
    }
    return result;
}

const expected_failure* find_expected_failure(const std::vector<expected_failure>& list, const std::string& file,
                                              const std::string& test_case, const std::string& test)
{
    for (const auto& entry : list) {
        if (entry.file == file && (!entry.test_case || *entry.test_case == test_case) && (!entry.test || *entry.test == test)) {
            return &entry;
        }
    }
    return nullptr;
}

struct file_result
{
    std::string file;
    std::size_t cases = 0;
    std::size_t tests = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;            // wrong valid/invalid answer
    std::size_t errored = 0;           // compile or validation threw
    std::size_t expected_failures = 0; // failed or errored, and listed as expected
    std::size_t unexpected_passes = 0; // passed, but listed as expected to fail
    std::vector<std::string> problems; // unexpected outcomes, for the log

    [[nodiscard]] bool ok() const { return problems.empty(); }
};

file_result run_file(const fs::path& tests_dir, const std::string& relative, const std::vector<expected_failure>& expected)
{
    file_result result;
    result.file = relative;

    std::ifstream in(tests_dir / relative);
    if (!in) {
        result.problems.push_back("cannot open " + (tests_dir / relative).string());
        return result;
    }
    const json document = json::parse(in);

    for (const auto& test_case : document.at("tests")) {
        if (const auto it = test_case.find("compatibility"); it != test_case.end() && !is_compatible(it->get<std::string>(), dialect_under_test)) {
            continue;
        }

        jsonschema::compile_options options;
        if (const auto tags = test_case.find("tags"); tags != test_case.end()) {
            for (const auto& tag : *tags) {
                if (tag == "format-assertion") {
                    options.formats = jsonschema::format_mode::assert;
                } else if (tag == "format-annotation") {
                    options.formats = jsonschema::format_mode::annotate;
                }
            }
        }
        if (const auto externals = test_case.find("externalSchemas"); externals != test_case.end()) {
            for (const auto& [retrieval_uri, external] : externals->items()) {
                options.resources.push_back({retrieval_uri, external});
            }
        }

        ++result.cases;
        const std::string case_description = test_case.at("description").get<std::string>();

        std::optional<jsonschema::schema> compiled;
        std::string compile_error;
        try {
            compiled.emplace(jsonschema::schema::compile(test_case.at("schema"), options));
        } catch (const std::exception& e) {
            compile_error = e.what();
        }

        for (const auto& test : test_case.at("tests")) {
            ++result.tests;
            const std::string test_description = test.at("description").get<std::string>();
            const bool expected_valid = test.at("valid").get<bool>();

            std::string outcome; // empty: passed
            bool errored = false;
            if (!compiled) {
                outcome = "schema did not compile: " + compile_error;
                errored = true;
            } else {
                try {
                    const bool actual_valid = compiled->is_valid(test.at("data"));
                    if (actual_valid != expected_valid) {
                        outcome = std::string("expected ") + (expected_valid ? "valid" : "invalid") + ", got " +
                                  (actual_valid ? "valid" : "invalid");
                    }
                } catch (const std::exception& e) {
                    outcome = std::string("validation threw: ") + e.what();
                    errored = true;
                }
            }

            const expected_failure* listed = find_expected_failure(expected, relative, case_description, test_description);
            if (listed) {
                listed->used = true;
            }
            const std::string label = "[" + case_description + "] " + test_description;
            if (outcome.empty()) {
                ++result.passed;
                if (listed) {
                    ++result.unexpected_passes;
                    result.problems.push_back(label + ": passed, but is listed as an expected failure (" + listed->reason + ")");
                }
            } else {
                (errored ? result.errored : result.failed) += 1;
                if (listed) {
                    ++result.expected_failures;
                } else {
                    result.problems.push_back(label + ": " + outcome);
                }
            }
        }
    }
    return result;
}

std::vector<std::string> all_suite_files(const fs::path& tests_dir)
{
    std::vector<std::string> files;
    for (const fs::path& sub : {fs::path{}, fs::path{"optional"}}) {
        const fs::path dir = tests_dir / sub;
        if (!fs::is_directory(dir)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back((sub / entry.path().filename()).generic_string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void print_markdown(const std::vector<file_result>& results)
{
    struct totals
    {
        std::size_t tests = 0, passed = 0, expected = 0;
    } required, optional;

    std::cout << "## JSON-Schema-Test-Suite (2020-12)\n\n";
    std::cout << "| File | Passed | Tests | Expected failures | Status |\n";
    std::cout << "|------|-------:|------:|------------------:|:------:|\n";
    for (const auto& r : results) {
        if (r.cases == 0) {
            continue;
        }
        auto& bucket = r.file.starts_with("optional/") ? optional : required;
        bucket.tests += r.tests;
        bucket.passed += r.passed;
        bucket.expected += r.expected_failures;
        std::cout << "| `" << r.file << "` | " << r.passed << " | " << r.tests << " | " << r.expected_failures << " | "
                  << (r.ok() ? (r.passed == r.tests ? "✅" : "☑️") : "❌") << " |\n";
    }
    const auto percent = [](std::size_t part, std::size_t whole) {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(1);
        out << (whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole)) << '%';
        return out.str();
    };
    std::cout << "\n**Required:** " << required.passed << " / " << required.tests << " (" << percent(required.passed, required.tests)
              << ")  \n**Optional:** " << optional.passed << " / " << optional.tests << " (" << percent(optional.passed, optional.tests)
              << ")\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path tests_dir;
    fs::path expected_failures_path;
    bool verbose = false;
    bool markdown = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--tests-dir" && i + 1 < argc) {
            tests_dir = argv[++i];
        } else if (arg == "--expected-failures" && i + 1 < argc) {
            expected_failures_path = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--markdown") {
            markdown = true;
        } else if (arg.starts_with("--")) {
            std::cerr << "unknown option " << arg << "\n";
            return exit_usage;
        } else {
            files.emplace_back(arg);
        }
    }
    if (tests_dir.empty()) {
        std::cerr << "usage: " << argv[0] << " --tests-dir DIR [--expected-failures FILE] [--verbose] [--markdown] [FILE...]\n";
        return exit_usage;
    }

    std::vector<expected_failure> expected;
    try {
        expected = load_expected_failures(expected_failures_path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return exit_usage;
    }

    const bool run_all = files.empty();
    if (run_all) {
        files = all_suite_files(tests_dir);
    }

    std::vector<file_result> results;
    bool any_applicable = false;
    bool all_ok = true;
    for (const auto& file : files) {
        file_result r;
        try {
            r = run_file(tests_dir, file, expected);
        } catch (const std::exception& e) {
            r.file = file;
            r.problems.push_back(std::string("runner error: ") + e.what());
        }
        any_applicable = any_applicable || r.cases > 0;
        all_ok = all_ok && r.ok();
        if (!markdown && (r.cases > 0 || !r.ok())) {
            std::cout << r.file << ": " << r.tests << " tests in " << r.cases << " cases: " << r.passed << " passed, " << r.failed
                      << " failed, " << r.errored << " errored (" << r.expected_failures << " expected failures, "
                      << r.unexpected_passes << " unexpected passes)\n";
            const std::size_t limit = verbose ? r.problems.size() : std::min(r.problems.size(), default_failure_listing_limit);
            for (std::size_t i = 0; i < limit; ++i) {
                std::cout << "  " << r.problems[i] << "\n";
            }
            if (limit < r.problems.size()) {
                std::cout << "  ... and " << (r.problems.size() - limit) << " more (use --verbose)\n";
            }
        }
        results.push_back(std::move(r));
    }

    // Entries that matched nothing are stale only when the whole suite was run.
    if (run_all) {
        for (const auto& entry : expected) {
            if (!entry.used) {
                std::cout << "expected-failures entry matched no test: " << entry.file
                          << (entry.test_case ? " [" + *entry.test_case + "]" : std::string())
                          << (entry.test ? " " + *entry.test : std::string()) << "\n";
                all_ok = false;
            }
        }
    }

    if (markdown) {
        print_markdown(results);
    }
    if (!any_applicable) {
        return exit_skipped;
    }
    return all_ok ? exit_ok : exit_failed;
}
