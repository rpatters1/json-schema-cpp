# 6. Testing, build, release

## 6.1 Tests

**Official suite.** `third_party/JSON-Schema-Test-Suite` is a git submodule pinned to a
commit and updated on purpose. A runner (`tests/suite_runner.cpp`) loads every file
under `tests/draft2020-12/`, registers `remotes/` with the registry under
`http://localhost:1234/`, and reports results per test case through CTest. Each suite
file becomes one CTest test, so failures point at a single file.

`tests/expected-failures.json` lists the exceptions: file, case description, test
description, and reason. The runner fails if something listed starts passing, so the
list can't go stale.

**Unit tests** (doctest; the suite runner uses it too):
- `Uri`: parse, resolve and normalize, including the RFC 3986 §5.4 tables.
- Registry: embedded resources, anchors, unknown-keyword pointers, loader errors.
- Each keyword: edge cases beyond the suite.
- Annotation retention: an `anyOf`/`oneOf`/`if` matrix with `unevaluated*`.
- Error delivery: only errors on the failing path, parents before children.
- Classification (doc 4): including the MNX schema and sample documents as fixtures.
- Output formats: golden JSON files for flag/basic/detailed/verbose.

**Carried over from pboettch/json-schema-validator.** Each is reviewed and rewritten
for 2020-12. They're kept only where they test behaviour that still applies:
- `string-format-check-test.cpp`: format cases.
- `test/issue-*`: `$id`/`$ref` resolution (issue-9, 12, 27, 48, 54, 70, 93, 96),
  error-message cases (issue-105, 117, 255) and entry selection (issue-149). The
  default-value cases (issue-25, 189, 229, 243) are dropped together with the feature.
- `uri.cpp`, `id-ref.cpp`: as ideas for new `Uri`/registry tests.

**Bowtie.** A small `bowtie` harness executable (reads JSON commands on stdin) is
added in milestone 3 so the project can be listed on bowtie.report.

**Fuzzing.** libFuzzer targets for the schema compiler (arbitrary schema JSON), the
`Uri` parser, and the regex translation layer. Run in CI for a short time on each PR,
and nightly for longer.

**Benchmarks.** Google Benchmark over the MNX schema with large MNX documents, plus a
few published schema/instance pairs, compared against the predecessor library.

## 6.2 Build

- CMake ≥ 3.21 and C++20.
- Dependencies:
  - `nlohmann_json` ≥ 3.11: required (`find_package`, or FetchContent fallback).
  - `PCRE2`: default on (`JSONSCHEMA_REGEX=pcre2|std`).
  - Test-only: doctest, Google Benchmark.
- Targets: `jsonschema::jsonschema` (static or shared, `BUILD_SHARED_LIBS`), plus the
  CLI `jsonschema-validate` (schema + instance files → output format of choice).
- Installed CMake package config, `CMakePresets.json`, and a pkg-config file.

## 6.3 CI and release

- GitHub Actions: Linux (GCC, Clang), macOS (AppleClang, built with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`), Windows (MSVC).
  Sanitizers (ASan, UBSan) on Linux.
- Suite results posted as a job summary, with the compliance badge generated from it.
- Release on tag `vX.Y.Z`: GitHub release, source archive, then Conan Center and
  vcpkg ports once stable (1.0).
- Semantic versioning. Before 1.0, minor releases may change the API.

## 6.4 Licence

MIT, Copyright (c) 2026 Robert Patterson. Hosted on the author's personal GitHub
account. Files adapted from pboettch/json-schema-validator keep their original copyright
line and are listed in `NOTICE`. The Ragel-generated email validator also credits
gene-hightower/smtp-address-validator.
