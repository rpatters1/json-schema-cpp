# json-schema-cpp

A complete JSON Schema **2020-12** validator for [nlohmann::json](https://github.com/nlohmann/json).

> Status: milestone 4c.
>
> **Works now:**
> - Every 2020-12 keyword, including `unevaluated*` and `$dynamicRef`.
> - Error reporting.
> - Annotations.
> - Schema classification (doc 4).
> - Meta-schema validation of schemas, with the 2020-12 meta-schemas built in.
> - `$vocabulary`.
> - The standard output formats `flag`, `basic` and `detailed`, and a command-line tool.
> - ECMA-262 regular expressions with Unicode support (`\p{…}`), using the vendored SRELL
>   engine.
>
> - `format` assertion for every 2020-12 format, including `idn-hostname` and `idn-email`
>   (UTS #46 and IDNA2008, Unicode 17.0.0), and custom formats.
> - The whole required JSON-Schema-Test-Suite passes.
>
> **Not yet:**
> - Content hooks (`contentEncoding`, `contentMediaType`, `contentSchema` assertion) and an
>   API for custom keywords (milestone 4d).
>
> See [docs/design](docs/design/README.md) and the [roadmap](docs/design/07-roadmap.md).

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

## Goals

- Full compliance with JSON Schema 2020-12: the required and optional parts of the
  official [JSON-Schema-Test-Suite](https://github.com/json-schema-org/JSON-Schema-Test-Suite).
- Built on `nlohmann::json`. Instances and schemas are ordinary `nlohmann::json` values.
- Compile once, validate many times. A compiled schema is immutable and thread-safe to share.
- Error messages a person can read, plus the spec's standard output formats.
- **Schema classification:** an optional callback that reports which named schema
  (for example a `$defs` entry) each instance location matched, collected during
  validation. Applications can build typed indexes such as id maps in the same pass.

## Non-goals

- Drafts other than 2020-12.
- Alternative JSON libraries (the internal design keeps this possible later).

## Acknowledgements

Some format checkers and regression tests are adapted from
[pboettch/json-schema-validator](https://github.com/pboettch/json-schema-validator)
(MIT, Copyright (c) 2016 Patrick Boettcher), which this project succeeds for
2020-12 users.
