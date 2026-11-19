# Design documents

| # | Document | Covers |
|---|----------|--------|
| 1 | [Scope and goals](01-scope.md) | What "complete" means, non-goals, compliance targets |
| 2 | [Architecture](02-architecture.md) | Registry, compiler, evaluator, annotation flow, `$dynamicRef` |
| 3 | [Public API](03-public-api.md) | Types, construction, validation entry points, output |
| 4 | [Schema classification](04-classification.md) | Reporting named-schema matches during validation (the id-map use case) |
| 5 | [Formats, regex, numbers, content](05-formats-regex-numbers.md) | Format vocabularies, the regex engine, numeric semantics, Unicode and IDNA, content assertion |
| 6 | [Testing, build, release](06-testing-build.md) | Test suite runner, CI, packaging, what is reused from pboettch/json-schema-validator |
| 7 | [Roadmap](07-roadmap.md) | Milestones and open questions |
| 8 | [Readiness for JSON Schema v1](08-v1-readiness.md) | What the v1 working draft changes, and the versioning strategy for it |

## Decisions so far

- 2020-12 only. A schema whose `$schema` names another dialect is rejected by default.
  An option treats it as 2020-12 anyway. JSON Schema v1 is expected to be handled by
  a later major version of the library, not by dual-dialect support (doc 8).
- Superseded keywords the spec still expects to work are supported: `definitions`
  always, `dependencies` by default with an opt-out (doc 1).
- Built on `nlohmann::json`, accessed through a narrow internal layer (see doc 2).
- A new repository, not a fork. Files reused from pboettch/json-schema-validator keep
  their MIT notices.
- Classification is designed in from the start and uses the same annotation mechanism
  as `unevaluated*`.
- snake_case naming throughout the public API.
- C++20 throughout, public headers included (`std::span`, designated initializers).
- Classification keeps working on invalid documents: content errors keep their
  classifications. Callbacks report locations with retained named matches (doc 4.6).
