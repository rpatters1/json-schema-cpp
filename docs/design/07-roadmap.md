# 7. Roadmap and open questions

## Milestones

| # | Milestone | Done when |
|---|-----------|-----------|
| 0 | **Skeleton** ✅ | CMake, CI matrix, suite submodule and runner (everything failing), JSON access layer, `Uri` with tests |
| 1 | **Core and validation keywords** | registry, compiler, evaluator without annotations; `$ref`/`$id`/`$anchor`/`$defs`; all validation-vocabulary keywords; applicators except `unevaluated*` |
| 2 | **Annotations** | frames, `unevaluated*`, `$dynamicRef`, buffered error delivery, classification (doc 4); **required suite passes 100%** |
| 3 | **Output and meta-schemas** | embedded meta-schemas and schema self-validation; `$vocabulary`; standard output formats; CLI; Bowtie harness |
| 4 | **Formats and regex** | PCRE2 backend with ECMA-262 translation; all format checkers incl. IDN/IRI; content hooks; custom keywords API |
| 5 | **mnxdom adoption** | mnxdom switches to json-schema-cpp; `IdIndexer.cpp` removed; MNX documents with stray properties are now rejected |
| 6 | **1.0** | optional suite at target; fuzzing clean; benchmarks published; docs; Conan/vcpkg |

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
