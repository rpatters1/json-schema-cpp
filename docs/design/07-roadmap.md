# 7. Roadmap and open questions

## Milestones

| # | Milestone | Done when |
|---|-----------|-----------|
| 0 | **Skeleton** ✅ | CMake, CI matrix, suite submodule and runner (everything failing), JSON access layer, `Uri` with tests |
| 1 | **Core and validation keywords** ✅ | registry, compiler, evaluator without annotations; `$ref`/`$id`/`$anchor`/`$defs`/`$dynamicRef`; all validation-vocabulary keywords; applicators except `unevaluated*`; suite gates CI through expected failures |
| 1a | **Readiness** ✅ | doc 8 (JSON Schema v1); `definitions` and `dependencies`; `evaluation_error`; milestone 2 naming fixed in docs 2 and 3 |
| 2 | **Annotations** ✅ | frames, `unevaluated*`, buffered error delivery and the error sink, classification (doc 4); **required suite passes, apart from format assertions and Unicode regex** (decided, open question 13) |
| 3a | **Meta-schemas** ✅ | embedded meta-schemas and schema self-validation; `$vocabulary` |
| 3b | **Output** | standard output formats; CLI |
| 3c | **Bowtie** | Bowtie harness |
| 4 | **Formats and regex** | PCRE2 backend with ECMA-262 translation; all format checkers incl. IDN/IRI; content hooks; custom keywords API |
| 5 | **mnxdom adoption** | mnxdom switches to json-schema-cpp; `IdIndexer.cpp` removed; MNX documents with stray properties are now rejected |
| 6 | **1.0** | optional suite at target; fuzzing clean; benchmarks published; docs; Conan/vcpkg |

**Milestone 1 results:**
- Required suite: 1,517 of 2,082 tests pass. Optional suite: 139 of 206.
- Every other test is listed in `tests/suite/expected-failures.json` with the
  milestone that fixes it. The 565 required failures are:
  - format assertion: 479, milestone 4.
  - `unevaluated*` annotations: 76, milestone 2.
  - Unicode regex: 9, milestone 4.
  - `$vocabulary`: 1, milestone 3.
- The 67 optional failures are (before the readiness change, which made the 14
  `dependencies` tests pass):
  - formats: 14.
  - regex: 25.
  - `$vocabulary`: 2.
  - `dependencies`: 14 (open question 12).
  - bignum: 10, permanent (doc 5.3).
  - cross-draft: 2, permanent (2020-12 only).
- `$dynamicRef` moved into milestone 1. It needs only the dynamic-scope stack, not
  annotations, and the evaluator had to carry that stack anyway.

**Milestone 2 results:**
- **Required suite:** 1,593 of 2,082 tests pass. The 489 that remain are all
  deferred to later milestones:
  - format assertion: 479, milestone 4;
  - Unicode regex: 9, milestone 4;
  - `$vocabulary`: 1, milestone 3.

  All 76 `unevaluated*` tests pass.
- **Optional suite:** 153 of 206.
- **Annotation suite:** 55 of 55.
- **Classification against mnxdom.** Its `IdIndexer.cpp` was compared with a
  classification sink written as in doc 4.7, on the 58 MNX example documents plus 30
  invalid variants of them (a stray property, or a misspelled `type` discriminator).
  All 88 produce the same ids, locations and type names, 499 ids in all. mnxdom can
  adopt the library (milestone 5) without waiting for milestones 3 and 4.
- **Cost on those 58 documents** (release build, Apple Silicon):

  | Mode | Time for all 58 |
  |---|---|
  | `is_valid` | 1.4 ms |
  | errors | 1.5 ms |
  | errors and classification | 3.3 ms |

**Milestone 3a results:**
- **Required suite:** 1,594 of 2,082. `vocabulary.json` now passes.
- **Optional suite:** 152 of 206. One fewer than before: the format-assertion test with
  a *required* format-assertion vocabulary used to pass by accident. It is now refused,
  as the spec requires, until milestone 4.
- The MNX schema validates against the 2020-12 meta-schema, and the comparison with
  mnxdom's `IdIndexer` is unchanged (88 of 88).

Classification is in milestone 2, not later, because it shares the annotation frames.
Building the two together keeps them on the same retention rules.

mnxdom could adopt the library as early as the end of milestone 2. The MNX schema
doesn't need formats, regex beyond simple `patternProperties`, or the output formats.

## Open questions

1. ~~Naming style~~: decided, snake_case.
2. ~~Minimum C++ standard~~: decided. C++20 throughout (doc 1).
3. **Regex.** Translate ECMA-262 to PCRE2 (proposed), or write a dedicated ECMA-262
   engine?
4. ~~Unclassified locations on invalid documents~~: decided. Recovery policy plus
   complete walk (doc 4.6), so mnxdom needs no walk of its own. Still open: whether
   the default discriminator rule is right for schemas other than MNX. Test it against
   a few public schemas (OpenAPI, GeoJSON, package.json) in milestone 2.
5. **Classification match policy.** The library reports every named match with a
   `via` tag and leaves the choice to the consumer (proposed). Should it also offer
   ready-made policies such as `outermost_ref_chain()`?
6. ~~Compatibility header~~: decided, none.
7. ~~Project basics~~: decided. doctest; personal GitHub account; MIT (Robert
   Patterson); nlohmann::json ≥ 3.11.
8. **Error messages.** English-only fixed text, with keyword and parameters exposed in
   `error` so callers can write their own (proposed)?
9. **IDNA Unicode tables.** Pin a Unicode version; commit the generated tables
   (proposed) or generate them at build time?
10. **Bignum.** Accept `optional/bignum` as a permanent documented exception
    (proposed), or plan a backend that keeps original number text?
11. **Package name** on Conan Center/vcpkg: confirm `json-schema-cpp` is free before
   the first release.
12. ~~`dependencies`~~: decided. Superseded keywords the spec still expects to work
    are supported (doc 1): `definitions` always, and `dependencies` by default behind
    `compile_options::legacy_dependencies`. `$recursiveRef`/`$recursiveAnchor` are not
    supported.
13. ~~What "required suite passes" means for milestone 2~~: decided. Milestone 2's
    target excludes the `format-assertion` cases and the Unicode regex tests. Both
    stay in milestone 4.
14. **When and how to support v1.** v1/2026 is still a working draft (doc 8).
    Proposed: stay "2020-12 only" until it is published, then release v1 support as a
    new major version rather than supporting both dialects in one build (doc 8.3).
    Still open: how long to maintain the 2020-12 major version after that.
