# 8. Readiness for JSON Schema v1

JSON Schema's next release is **v1**. Its first edition is **v1/2026**, and it
replaces the old "draft" naming. Later editions are yearly, and each one guarantees
compatibility with earlier v1 schemas.

As of 2026-11-18, v1 is **not released**. json-schema.org still lists 2020-12 as
current. This document records what the working draft changes and what that means
for json-schema-cpp. The goal is that a later library version can switch to v1 without reworking the
architecture.

Implementing v1 now is **not** proposed. Doc 1 says "2020-12 only", and that stays
until v1 ships (open question 14).

Sources:
- the spec repository, json-schema-org/json-schema-spec, at `4e87b91` (2026-11-15):
  `specs/jsonschema-core.md`, `specs/jsonschema-validation.md`,
  `specs/meta/meta.schema.json`, `specs/output/`, and `adr/`;
- the JSON-Schema-Test-Suite at `e2a68a1` (2026-11-16).

## 8.1 Identification

- Dialect URI (`$schema`): `https://json-schema.org/v1`. The meta-schema's `$id` is
  `https://json-schema.org/v1/2026`.
- There is a single meta-schema. 2020-12 has one meta-schema per vocabulary.
- In the test suite, a release is identified by its year in `compatibility`
  (`2026`, `2027`, …). `9999` means "in the spec but not yet released". Running the
  suite's compatibility algorithm at 2020 and at 9999 shows 44 cases that apply only
  to 2020-12 and 26 that apply only to v1. Section 8.2 is drawn from those cases and
  from the spec text.

## 8.2 What changes

| Area | 2020-12 (today) | v1 working draft | Impact on json-schema-cpp |
|------|-----------------|------------------|---------------------------|
| **Unknown keywords** | Ignored, collected as annotations | MUST refuse to process the schema. `x-*` keywords are annotation-only. The meta-schema has `unevaluatedProperties: false` and allows `$`-keywords only from a fixed list. | A v1 version changes the compiler's "ignore unknown keywords" branch into a compile error, except for `x-*` and registered keywords (doc 3.5). |
| **`$dynamicRef`** | URI reference. Resolved statically first, and dynamic only if the target is a matching `$dynamicAnchor` ("bookend" rule) | A **plain anchor name** (`"items"`, not `"#items"`). Resolved only through the dynamic scope (outermost matching resource). No bookend. No match is a **resolution failure**. | A v1 version rewrites the keyword's factory and evaluation: new value syntax, no static target. The candidate binding built at compile time (doc 2.6) still applies. A missing anchor becomes an `evaluation_error`, not `false`. |
| **`format`** | Annotation by default. Assertion when configured, or through the format-assertion vocabulary | **Assertion by default.** An unsupported format value MUST be refused (compile error). Annotation-only use moves to `x-format`. | A v1 version flips the default of `compile_options::formats` and turns an unknown format into a compile error. |
| **Format details** | RFC 3339 times allow leap seconds. `hostname` follows RFC 1123 | `date-time` accepts RFC 9557 suffixes. Leap seconds are invalid times. `hostname` has no IDNA A-label rules. The spec repo keeps a format registry that implementations SHOULD support (40 entries: `int32`, `base64url`, `commonmark`, …). | A v1 version swaps those checkers. Milestone 4 keeps each one a separate function so the swap is local. |
| **`$vocabulary`** | Core keyword, with optional and required vocabularies | Moved to a **proposal**. It is not part of v1 | Milestone 3's `$vocabulary` work is 2020-12-only. Keep it isolated so a v1 version can delete it. |
| **New keywords** | — | `propertyDependencies`, a proposal (in-place applicator, visible to `unevaluated*`). Tests are in `proposals/`. | No new mechanism needed: it is one more in-place keyword. The annotation design must not assume a closed set of in-place applicators. |
| **Dialect determination** | One dialect assumed | Determined **per schema resource**: `$schema`, else the parent resource's dialect, else external context or user configuration, else MUST refuse. Embedded resources MAY use a different dialect. `$schema` MUST NOT appear in a non-resource subschema. | Only matters for dual support (8.3). A v1 version checks `$schema` against v1 instead of 2020-12, and treats a stray `$schema` in a non-resource subschema as an error. |
| **References to non-schemas** | The suite expects `$ref` into unknown keywords to work | Undefined behavior. Implementations MAY support it | No change needed. On-demand discovery (doc 2.3) is allowed in both. |
| **Unresolvable references** | Not specified | MUST halt with an "indeterminate" result, neither pass nor fail, reported by an exception or similar | Compile-time resolution already does this for `$ref`. Runtime cases (the `$dynamicRef` recursion guard today, v1's unmatched `$dynamicRef` later) throw `evaluation_error` (8.3). |
| **`content*`** | Annotations, with optional validation | Implementations **MUST NOT automatically** decode or validate. | The content hooks planned in doc 3.3 must stay opt-in, which they already are. |
| **Regex** | ECMA-262 | ECMA-262 (17th ed.) with the `u` flag. An engine that can't give ECMA-262 results for a pattern MUST refuse the schema. | The PCRE2 translation layer (doc 5.2) must reject what it can't translate faithfully rather than approximate it. The interim `std::regex` backend doesn't meet this. |
| **Output** | §12: `flag`, `basic`, `detailed`, `verbose`. Units carry `keywordLocation` and `absoluteKeywordLocation` | A separate spec. Formats `flag`, `list`, `hierarchical`. Units carry **Evaluation Path**, **Schema Location** and Instance Location, plus optional *dropped annotations* from failed subschemas | Milestone 2 names fields after v1's terms. The annotation design must be able to keep annotations from failed branches when asked. |
| **`contains` on objects** | Arrays only | Still arrays only (ADR 2024-02) | None. |

## 8.3 Strategy: versioning, not dual support

How much 2020-12 support to keep once v1 ships is undecided. The preferred approach is
to **manage it with library versions**, not to run both dialects in one build. For
example, 1.x implements 2020-12, and a later major version implements v1. Users stay
on the major version that matches their schemas.

That rules out large architectural changes now. It is also cheap for this project's
main user: checked on 2026-11-18, the MNX schema uses no keyword that v1 rejects.
There are no unknown keywords, no `format`, no `$dynamicRef` and no `$vocabulary`, so
moving mnxdom to a v1 release would need only a new `$schema` URI.

Switching versions is easy because the differences are already in a few places:
- the keyword table (`src/keywords/keywords.cpp`);
- the `$dynamicRef` keyword (`src/keywords/core.cpp`);
- the "ignore unknown keywords" branch in `compiler::compile_node`;
- the dialect check;
- the default `format` mode.

A v1 version edits those. The registry, the node graph and the evaluator stay as they
are.

### Small things to do in milestone 2 anyway

They cost nothing extra and make the change of version smaller:

1. **Name error and output fields after v1:** `evaluation_path`, `schema_location`
   and `instance_location`. They describe 2020-12's `keywordLocation` and
   `absoluteKeywordLocation` equally well, and the 2020-12 §12 serializer maps them
   back to the old JSON names.
2. **Make keeping "dropped" annotations possible.** Frames can keep a failed branch's
   annotations when output asks for them. 2020-12 `verbose` output benefits too. The
   fast paths keep discarding them.
3. **Add `evaluation_error`** for validation-time failures that are neither valid nor
   invalid (today: `$dynamicRef` recursion). Then `is_valid` doesn't throw
   `schema_error`.
4. **Keep dialect-specific behavior inside keyword factories and the compiler's
   policy points** (the list above), not scattered through the evaluator.

### If running both dialects in one build is ever wanted

This is not planned. It would need:
- a dialect field on each schema resource, set during discovery from `$schema` or the
  parent resource;
- keyword tables and policies chosen per dialect;
- a `default_dialect` option;
- `formats` defaulting per dialect;
- a `--dialect` flag on the suite runner.

The design doesn't block any of these, but none is worth doing before it's needed.

## 8.4 Later milestones

- **Milestone 3:**
  - Embed the 2020-12 meta-schemas and keep `$vocabulary` 2020-12-only.
  - A v1 version embeds its single meta-schema instead.
  - Output serializers produce 2020-12's formats and v1's `list` and `hierarchical`
    from the same internal results.
- **Milestone 4:**
  - Format checkers are separate functions per behavior, so that a v1 version can
    swap `date-time`, `time` and `hostname` rules without touching the rest.
  - The registry formats (`int32`, …) are optional extras.
  - The regex layer refuses patterns it can't evaluate exactly.
- **Doc 3.5 custom keywords:** registering a keyword also makes it "known", so a v1
  version's unknown-keyword rule accepts it.

## 8.5 Deferred until v1 ships

- The v1 keyword table and `$dynamicRef` semantics.
- The v1 format behavior and the v1 meta-schema.
- Switching on the v1 suite run.
- The `propertyDependencies` proposal: add it when its tests leave `proposals/`.
