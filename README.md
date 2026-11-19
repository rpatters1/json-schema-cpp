# json-schema-cpp

A complete JSON Schema **2020-12** validator for [nlohmann::json](https://github.com/nlohmann/json).

> **Version 1.0.0.**
> - Every 2020-12 keyword, including `unevaluated*` and `$dynamicRef`. The whole
>   required JSON-Schema-Test-Suite passes, and the optional suite passes apart from
>   bignum and cross-draft cases.
> - Error reporting, annotations, and the standard output formats `flag`, `basic` and
>   `detailed`, plus a command-line tool.
> - Schema classification (doc 4).
> - Meta-schema validation of schemas, with the 2020-12 meta-schemas built in, and
>   `$vocabulary`.
> - ECMA-262 regular expressions with Unicode support (`\p{…}`), using the vendored SRELL
>   engine.
> - `format` assertion for every 2020-12 format, including `idn-hostname` and `idn-email`
>   (UTS #46 and IDNA2008, Unicode 17.0.0), and custom formats.
> - Opt-in content assertion (`contentEncoding`, `contentMediaType`, `contentSchema`),
>   with pluggable decoders and parsers.
> - Custom keywords: assertions and applicators, optionally tied to a `$vocabulary`.
>
> See the [changelog](CHANGELOG.md), [docs/design](docs/design/README.md) and the
> [roadmap](docs/design/07-roadmap.md).

```cpp
#include <jsonschema/jsonschema.hpp>

auto schema = jsonschema::schema::compile(nlohmann::json::parse(schema_text));
bool ok = schema.is_valid(nlohmann::json::parse(document_text));

jsonschema::collecting_error_sink errors;
auto result = schema.validate(document, {.errors = &errors});
for (const auto& e : errors.errors) {
    std::cout << e.instance_location.to_string() << ": " << e.message << "\n";
}
```

## Command line

```sh
json-schema-cpp schema.json document.json...              # text: valid, or each error
json-schema-cpp --output detailed schema.json doc.json    # 2020-12 output format (flag|basic|detailed)
cat doc.json | json-schema-cpp schema.json                # standard input
```

- `$ref`s to files next to the schema are read from disk, because the schema's base URI
  is its `file:` URI. Nothing is fetched over the network.
- `--resource URI=FILE` registers other schemas.
- Exit status: 0 if every document is valid, 1 if any is invalid, 2 on errors such as
  an unreadable file or an invalid schema.
- `--version` prints the version, and `--help` lists every option.

## Using the library

Each [release](https://github.com/rpatters1/json-schema-cpp/releases) has a source
tarball, `json-schema-cpp-X.Y.Z.tar.gz`, and its SHA-256 in `SHA256SUMS`. Use the
tarball rather than GitHub's automatic "Source code" archives: it includes the test
suite.

**Installed** (`cmake --install`, or a distribution package):

```cmake
find_package(jsonschema 1.0 REQUIRED)
target_link_libraries(app PRIVATE jsonschema::jsonschema)
```

```sh
c++ -std=c++20 app.cpp $(pkg-config --cflags --libs jsonschema)
```

**Built with your project:**

```cmake
include(FetchContent)
FetchContent_Declare(jsonschema
    URL https://github.com/rpatters1/json-schema-cpp/releases/download/v1.0.0/json-schema-cpp-1.0.0.tar.gz
    URL_HASH SHA256=<from SHA256SUMS>)
FetchContent_MakeAvailable(jsonschema)
target_link_libraries(app PRIVATE jsonschema::jsonschema)
```

The version is available at compile time (`JSONSCHEMA_VERSION_STRING`,
`JSONSCHEMA_VERSION_MAJOR`/`MINOR`/`PATCH` in `<jsonschema/version.hpp>`) and at run
time (`jsonschema::version()`, the version the library binary was built as).

Versioning is semantic: 1.x releases keep the API compatible. The shared library's
SONAME carries the major and minor version.

## Building

```sh
git clone --recurse-submodules https://github.com/rpatters1/json-schema-cpp.git
cd json-schema-cpp
cmake --preset debug
cmake --build --preset debug
ctest --preset debug        # unit tests
ctest --preset suite        # JSON-Schema-Test-Suite (2020-12), one test per suite file

# Per-file pass counts for the whole suite:
build/debug/bin/jsonschema_suite_runner --tests-dir third_party/JSON-Schema-Test-Suite/validation/tests \
    --expected-failures tests/suite/expected-failures.json --markdown
```

Requirements: CMake ≥ 3.21 (presets need 3.25), a C++20 compiler, and
nlohmann_json ≥ 3.11. nlohmann_json is fetched automatically if no installed copy is
found. macOS builds support a deployment target of 10.15.

For distribution packages, build the release tarball with
`-DJSONSCHEMA_USE_SYSTEM_NLOHMANN_JSON=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON`. It
then needs no network access, and `ctest` runs the unit tests and the whole suite.
`-DJSONSCHEMA_USE_SYSTEM_SRELL=ON` uses an installed SRELL instead of the vendored copy.

## Goals

- Full compliance with JSON Schema 2020-12: the required and optional parts of the
  official [JSON-Schema-Test-Suite](https://github.com/json-schema-org/JSON-Schema-Test-Suite).
- Built on `nlohmann::json`. Instances and schemas are ordinary `nlohmann::json` values.
- Compile once, validate many times. A compiled schema is immutable and thread-safe to share.
- Error messages a person can read, plus the spec's standard output formats.
- **Schema classification:** an optional callback that reports which named schema
  (for example a `$defs` entry) matched during evaluation. Branch begin, commit,
  and discard events let applications build typed indices in the same pass.

## Non-goals

- Drafts other than 2020-12.
- Alternative JSON libraries (the internal design keeps this possible later).

## Acknowledgements

Some format checkers and regression tests are adapted from
[pboettch/json-schema-validator](https://github.com/pboettch/json-schema-validator)
(MIT, Copyright (c) 2016 Patrick Boettcher), which this project succeeds for
2020-12 users.
