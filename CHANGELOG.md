# Changelog

All notable changes to json-schema-cpp are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

## [1.0.0] - 2026-11-19

The first release: a complete JSON Schema 2020-12 validator for nlohmann::json.

### Validation
- Every 2020-12 keyword, including `unevaluated*`, `$dynamicRef` and `$vocabulary`.
- The superseded `definitions` and `dependencies` keywords. Setting
  `compile_options::legacy_dependencies` to false turns `dependencies` off.
- JSON-Schema-Test-Suite: required 2,082 / 2,082, optional 194 / 206. The other 12 are
  bignum and cross-draft cases, which are permanent exceptions.
- Compile once, validate many times. A compiled schema is immutable and safe to share
  between threads.

### Results
- Errors with evaluation path, schema location and instance location, through error
  sinks.
- Annotations, and the 2020-12 output formats `flag`, `basic` and `detailed`.
- Schema classification: reports which named subschemas matched which instance
  locations, streamed during evaluation.

### Schemas and resources
- Embedded 2020-12 meta-schemas, with meta-schema validation of schemas.
- `$ref` across documents, through `compile_options::resources` and a `schema_loader`
  for unknown URIs.

### Formats, regex and content
- ECMA-262 regular expressions with Unicode property escapes, using the vendored SRELL
  engine. `std::regex` is a build option.
- `format` assertion for every 2020-12 format, including `idn-hostname` and
  `idn-email` (UTS #46 and IDNA2008, Unicode 17.0.0), plus custom formats.
- Opt-in content assertion for `contentEncoding`, `contentMediaType` and
  `contentSchema`, with pluggable decoders and parsers.

### Extensions
- Custom keywords: assertions and applicators, optionally tied to a `$vocabulary`.

### Tools
- The `json-schema-cpp` command-line validator.
- A Bowtie harness (`tools/bowtie`).

### Packaging
- CMake package `jsonschema` (`find_package(jsonschema 1.0)`, target
  `jsonschema::jsonschema`) and the pkg-config module `jsonschema`.
- The version is available at compile time through `<jsonschema/version.hpp>`, and at
  run time through `jsonschema::version()` and `json-schema-cpp --version`.
- A reproducible release tarball that includes the test suite, with SHA-256 checksums.

[1.0.0]: https://github.com/rpatters1/json-schema-cpp/releases/tag/v1.0.0
