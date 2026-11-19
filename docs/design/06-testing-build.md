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
- **Bowtie** (`tools/bowtie`): a harness for Bowtie's IHOP protocol, a container image,
  and `run-bowtie.sh`, which runs Bowtie's validation and annotation suites locally. The
  CTest test `bowtie/session` checks the protocol in CI without Bowtie.
- `annotations/tests/*.json` is a separate **annotation** suite.
  - `tests/suite/annotation_runner.cpp` runs it as the CTest test `annotations`, with
    labels `suite` and `suite-annotations`. All 55 tests for 2020-12 pass.
  - The suite writes schema locations as pointers from the document root. The runner
    resolves `$id` along each pointer to compare with the library's canonical absolute
    locations.

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
  - Regex: SRELL, vendored, by default; `JSONSCHEMA_USE_SYSTEM_SRELL` uses an installed
    copy, and `JSONSCHEMA_REGEX=std` uses `std::regex` instead (doc 5.2).
  - Test-only: doctest (header only; fetched without running its CMake, which CMake 4
    rejects), Google Benchmark.
- Sources compile into an object library (`jsonschema_objects`). The public library is
  built from it, and unit tests link it directly, so internal functions are testable in
  shared builds too.
- Targets: `jsonschema::jsonschema` (static or shared, `BUILD_SHARED_LIBS`), plus the
  CLI `json-schema-cpp` (schema + instance files → output format of choice).
- Installed: the CMake package `jsonschema` (`find_package(jsonschema 1.0)`), the
  pkg-config file `jsonschema.pc`, the generated `export.hpp` and `version.hpp`, the CLI,
  and the licences. `CMakePresets.json` covers development builds.
- Version: `project(VERSION)` in the top-level `CMakeLists.txt` is the only source.
  `version.hpp` turns it into `JSONSCHEMA_VERSION_MAJOR/MINOR/PATCH/STRING` and
  `jsonschema::version_major/minor/patch`. `jsonschema::version()` returns the version
  the library binary was built as. `json-schema-cpp --version` prints it.
- Distribution builds: `-DJSONSCHEMA_USE_SYSTEM_NLOHMANN_JSON=ON
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON` builds and tests the release tarball with no
  network access. doctest is found with `find_package` when installed.

## 6.3 CI and release

- GitHub Actions: Linux (GCC, Clang), macOS (AppleClang, built with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`), Windows (MSVC).
  One Linux job builds with `JSONSCHEMA_REGEX=std`, so the `std::regex` option keeps working.
  Sanitizers (ASan, UBSan) on Linux.
- Every build-and-test job runs unit and suite tests. A separate job posts the suite
  table as the job summary; the compliance badge will be generated from it.
- A job installs the library (static and shared) and builds `tests/package`, a separate
  consumer project, against it with `find_package` and with pkg-config
  (`tools/release/check-install.sh`).
- **Release** (`.github/workflows/release.yml`), on a tag `vX.Y.Z`:
  1. The tag must equal the project version, and `CHANGELOG.md` must have a section
     for it.
  2. `tools/release/make-dist.sh` builds `json-schema-cpp-X.Y.Z.tar.gz` and
     `SHA256SUMS`. Unlike GitHub's automatic archives, the tarball includes the
     JSON-Schema-Test-Suite submodule. It is reproducible: the same commit always gives
     the same SHA-256.
  3. The tarball is built as a distribution would build it (shared, prefix `/usr`,
     system nlohmann_json and doctest, no network), tested, installed with `DESTDIR`,
     and checked with the consumer project.
  4. A build-provenance attestation, then a GitHub release with the tarball,
     `SHA256SUMS`, and the `CHANGELOG.md` section as notes.

  Run by hand (`workflow_dispatch`), it does everything except publish.
- **Versioning:** semantic versioning of the API. The CMake package is
  `SameMajorVersion`-compatible. The shared library's `SOVERSION` is `MAJOR.MINOR`, so
  a minor release that changes the ABI gets a new SONAME.
- Conan Center and vcpkg ports come after 1.0 (doc 7).

## 6.4 Licence

MIT, Copyright (c) 2026 Robert Patterson. Hosted on the author's personal GitHub
account. Files adapted from pboettch/json-schema-validator keep their original copyright
line and are listed in `NOTICE`. The Ragel-generated email validator also credits
gene-hightower/smtp-address-validator.
