# Bowtie harness

[Bowtie](https://bowtie.report) runs JSON Schema implementations against the official
test suite through a small stdin/stdout protocol (IHOP, version 1). `harness.cpp` speaks
that protocol for json-schema-cpp. It answers `start`, `dialect`, `run` and `stop`, and it
supports both of Bowtie's output modes: `flag`, and `annotations`, which comes from the
annotation sink.

```sh
tools/bowtie/run-bowtie.sh       # build the image, run both suites, print summaries
```

The script builds `localhost/cpp-json-schema-cpp` from `Dockerfile`: a static
Alpine binary, about 17 MB. It then runs `bowtie suite` and `bowtie annotation-suite`
against the test-suite checkout in `third_party/`, and leaves the reports in
`build/bowtie/`.

The harness is also built with the tests (`JSONSCHEMA_BUILD_BOWTIE`). The CTest test
`bowtie/session` replays `session.jsonl` and compares the replies with
`session.expected.jsonl`, so CI checks the protocol on every platform without Bowtie.

## Publishing to bowtie.report

Bowtie no longer keeps harnesses in its own repository. Each implementation has a
repository in the bowtie-json-schema organization, named `<language>-<name>`. The
existing C++ ones are `cpp-blaze`, `cpp-jsoncons`, `cpp-valijson`, and so on. Its CI
calls Bowtie's reusable `harness-ci.yml` and `harness-report.yml` workflows, which build
and publish the image and its report.

**Bowtie does not accept contributions made with any large language model**
(`docs/contributing.rst` in bowtie-json-schema/bowtie). Violations get the contributor
banned. Everything submitted to Bowtie must be written by hand, so this repository
holds no files intended for it.

Checklist:
1. Open an issue at bowtie-json-schema/bowtie asking for a `cpp-json-schema-cpp`
   harness repository. Link this repository and a release.
2. The harness image, modeled on `cpp-blaze`:
   - an Alpine base, multi-stage;
   - dependencies from `apk` (`g++ cmake ninja nlohmann-json`);
   - download `json-schema-cpp-X.Y.Z.tar.gz` from the GitHub release and check it
     against the SHA-256 in that release's `SHA256SUMS`;
   - build the `jsonschema_bowtie_harness` target with the options `Dockerfile` here
     uses (tests, CLI and install off; system nlohmann_json; static libstdc++);
   - copy the binary into a plain Alpine image as the `CMD`.

   Bowtie asks for harness code to live in the harness repository. Whether it can
   build `harness.cpp` from the release tarball, rather than keep its own copy, is for
   the Bowtie maintainers to decide.
3. Run `bowtie smoke -i <image>` and `run-bowtie.sh` here before submitting.
4. Each new release means bumping the version and SHA-256 in the harness repository.
   Dependabot can't track a tarball URL.
