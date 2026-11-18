# json-schema-cpp

A complete JSON Schema **2020-12** validator for [nlohmann::json](https://github.com/nlohmann/json).

> Status: design phase. No code yet. See [docs/design](docs/design/README.md).

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
