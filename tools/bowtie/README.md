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

Publishing to bowtie.report means a pull request to bowtie-json-schema/bowtie and a
public repository. That waits until 1.0 is code-complete (doc 7).
