# 1. Scope and goals

## Compliance target

"Complete" means:

1. **All required tests** in `JSON-Schema-Test-Suite/tests/draft2020-12/*.json` pass.
2. **All optional tests** in `tests/draft2020-12/optional/**` pass, except those listed
   in a short, documented exceptions file. Each exception gives a reason. The goal is
   zero exceptions. `bignum` and `float-overflow` are the likeliest to remain (doc 5).
3. **Meta-schema validation:** a schema can be checked against the 2020-12 meta-schema
   and its vocabulary meta-schemas, which are embedded in the library.
4. **Listed on [Bowtie](https://bowtie.report)**, the cross-implementation compliance
   harness, with a published score.

## Vocabularies

| Vocabulary | Keywords | Behaviour |
|------------|----------|-----------|
| core | `$schema` `$id` `$ref` `$anchor` `$dynamicRef` `$dynamicAnchor` `$vocabulary` `$comment` `$defs` | full |
| applicator | `allOf` `anyOf` `oneOf` `not` `if` `then` `else` `dependentSchemas` `prefixItems` `items` `contains` `properties` `patternProperties` `additionalProperties` `propertyNames` | full |
| unevaluated | `unevaluatedItems` `unevaluatedProperties` | full |
| validation | `type` `enum` `const` `multipleOf` `maximum` `exclusiveMaximum` `minimum` `exclusiveMinimum` `maxLength` `minLength` `pattern` `maxItems` `minItems` `uniqueItems` `maxContains` `minContains` `maxProperties` `minProperties` `required` `dependentRequired` | full |
| meta-data | `title` `description` `default` `deprecated` `readOnly` `writeOnly` `examples` | annotations |
| format-annotation | `format` | annotation by default. Assertion is opt-in (doc 5) |
| format-assertion | `format` | assertion when a meta-schema requires this vocabulary |
| content | `contentEncoding` `contentMediaType` `contentSchema` | annotations, plus an optional checker hook |

Unknown keywords are kept as annotations, as the spec requires. They are reachable by
JSON-pointer `$ref`, but the compiler does not look inside them for `$id`.

`$vocabulary` (implemented in milestone 3):
- A vocabulary set to `true` in a custom meta-schema that the library does not
  recognize is a compile error.
- An unrecognized one set to `false` is ignored.
- A known vocabulary that is left out disables its keywords, which are then unknown
  keywords and are collected as annotations. Core always applies.
- The vocabularies come from the meta-schema that `$schema` names directly. If it has
  no `$vocabulary`, all 2020-12 vocabularies apply.
- A schema resource without `$schema` takes its enclosing resource's vocabularies.
- Until milestone 4, format-assertion counts as unrecognized: a meta-schema that
  requires it is refused, as the spec says for unsupported vocabularies.

## Library qualities

- **Compile once, validate many times.** Validation is `const`, does not allocate on
  success paths where possible, and is thread-safe.
- **Fail fast when you only need yes/no.** Validation stops early when the caller asks
  only for valid/invalid and no annotations are needed (doc 2).
- **Readable errors.** Every error carries the instance location, keyword location,
  absolute keyword location and a short English message.
- **No exceptions for invalid instances.** Exceptions are reserved for invalid schemas,
  failed `$ref` resolution, and loader failures.
- **Language standard.** C++20 throughout (decided; see below).

## Language standard

**Decided: C++20 for both the public headers and the implementation.** In CMake that's
`target_compile_features(jsonschema PUBLIC cxx_std_20)`. mnxdom will move to C++20
when it adopts this library.

What this gives the API:
- `std::span` directly, with no library-specific span type. That avoids the
  ODR/ABI trap of a type that changes with each consumer's language mode.
- Designated initializers for options structs: `validate(doc, {.errors = &sink})`.
- Concepts where they make extension points clearer (custom keywords, format
  checkers), rather than only virtual interfaces.
- `<compare>`, `consteval` and `std::string_view`/`std::span` everywhere.

Every mainstream 2026 toolchain supports this: GCC 11+, Clang 14+, MSVC 2019 16.10+,
and current AppleClang.

### Minimum deployment target: macOS 10.15 (hard requirement)

The library must build and run with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`. Apple's
libc++ gates some C++17/20 library features on the deployment target, so these are
**not allowed** in headers or implementation:
- `std::format` and floating-point `std::to_chars`/`std::from_chars` (macOS 13.3+).
  Number formatting goes through nlohmann's own serializer or `snprintf`.
- `std::pmr` memory resources (macOS 14+). The per-call arena (doc 2.7) is our own.
- `std::jthread`/`std::stop_token` and `<syncstream>`, which are unnecessary anyway.

These are fine on 10.15: `std::span`, `<bit>`, concepts, ranges, `std::string_view`,
`std::optional`/`std::variant`, `std::filesystem` (10.15 is the first macOS with it),
and `std::shared_mutex`.

CI enforces this: the macOS job builds with the 10.15 deployment target. Using a
gated feature fails at compile time with an "unavailable" error, so mistakes are
caught on the pull request.

## Superseded keywords

The 2020-12 meta-schema still describes four keywords that 2020-12 replaced, all
marked deprecated. They are handled as follows (decided):

| Keyword | Handling | Why |
|---------|----------|-----|
| `definitions` | Treated exactly like `$defs`, always | The validation spec (Appendix A) says implementations SHOULD. It only affects where identifiers are found, never validity. |
| `dependencies` | Evaluated as `dependentRequired` (array members) or `dependentSchemas` (schema members). On by default; `compile_options::legacy_dependencies = false` ignores it | The spec doesn't require it, but a 2020-12 schema that uses it almost always means it, and ignoring it silently would leave the constraint unenforced. The optional `dependencies-compatibility` tests expect it. |
| `$recursiveRef`, `$recursiveAnchor` | Not supported (ignored as unknown keywords) | 2019-09's mechanism with different semantics. The spec doesn't expect it, and the suite doesn't test it for 2020-12. |

None of these exist in JSON Schema v1 (doc 8).

## Non-goals

- Drafts other than 2020-12, including 2019-09's `$recursiveRef`.
- Default-value injection or instance patching. This was a feature of the predecessor
  library. Callers can rebuild it from `default` annotations if wanted.
- Network access. Remote schemas are fetched by a loader the caller supplies.
- Code generation from schemas.
