# 6. Testing, build, release

## 6.1 Tests

**Official suite.** `third_party/JSON-Schema-Test-Suite` is a git submodule pinned to a
commit and updated on purpose. Since 2026 the suite is organized like this:

- `validation/tests/*.json` and `validation/tests/optional/*.json` hold every dialect's
  tests in one set of files. Each test case may carry a `compatibility` expression
  (e.g. `"7"`, `"<=2019"`, `"=2020"`). The runner uses the suite's published matching
  algorithm to select 2020-12 cases.
- Schemas carry no `$schema`, so the dialect comes from configuration. That fits the
  default here, which is that a schema with no `$schema` is 2020-12.
- Remote schemas live in each case's `externalSchemas` map (retrieval URI → schema), not
  a shared `remotes/` directory. They must be scoped per test case, which is passed as
  `compile_options::resources`.
- `tags` of `format-assertion` / `format-annotation` select the format mode.
- `proposals/` (unreleased keywords) is not run.
- `annotations/tests/*.json` is a separate **annotation** suite. It gets its own runner
  in milestone 2, where it checks the annotation collection that `unevaluated*` and
  classification depend on.

At the pinned commit, 2020-12 has **2,082 required tests** (431 cases in 59 files) and
**206 optional tests** (62 cases in 11 files).

The runner (`tests/suite/suite_runner.cpp`) runs one file per CTest test (`suite/<name>`,
labels `suite` plus `suite-required` or `suite-optional`). Files with no 2020-12 cases
exit with 77, which CTest reports as skipped. `--markdown` prints a per-file table and
totals, which CI posts as the job summary.

`tests/suite/expected-failures.json` lists the accepted exceptions. Each entry has a
file, an optional case description, an optional test description, and a reason.
Leaving out the case or test means "all" of them. The runner fails if a listed test
passes, and, on a full run, if an entry matches nothing. That keeps the list from
going stale. Each reason names the milestone that removes it, or says "permanent". An entry with `"varies": true` may pass or fail; it is for outcomes that
differ by platform, such as the interim `std::regex` backend on libstdc++ versus libc++.
`--emit-failures` prints the failing tests that are not yet listed, in the file's
entry format, so the list is regenerated rather than written by hand.

With that list in place, every CI build-and-test job runs the suite as part of
`ctest` (since milestone 1). A regression in any file fails the build on every
platform, including the sanitizer job.

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
  - Test-only: doctest (header only; fetched without running its CMake, which CMake 4
    rejects), Google Benchmark.
- Sources compile into an object library (`jsonschema_objects`). The public library is
  built from it, and unit tests link it directly, so internal functions are testable in
  shared builds too.
- Targets: `jsonschema::jsonschema` (static or shared, `BUILD_SHARED_LIBS`), plus the
  CLI `jsonschema-validate` (schema + instance files → output format of choice).
- Installed CMake package config, `CMakePresets.json`, and a pkg-config file.

## 6.3 CI and release

- GitHub Actions: Linux (GCC, Clang), macOS (AppleClang, built with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`), Windows (MSVC).
  Sanitizers (ASan, UBSan) on Linux.
- Every build-and-test job runs unit and suite tests. A separate job posts the suite
  table as the job summary; the compliance badge will be generated from it.
- Release on tag `vX.Y.Z`: GitHub release, source archive, then Conan Center and
  vcpkg ports once stable (1.0).
- Semantic versioning. Before 1.0, minor releases may change the API.

## 6.4 Licence

MIT, Copyright (c) 2026 Robert Patterson. Hosted on the author's personal GitHub
account. Files adapted from pboettch/json-schema-validator keep their original copyright
line and are listed in `NOTICE`. The Ragel-generated email validator also credits
gene-hightower/smtp-address-validator.
