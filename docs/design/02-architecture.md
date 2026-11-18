# 2. Architecture

```
 schema JSON ──► Registry ──► Compiler ──► CompiledSchema (immutable graph)
                  ▲   │                         │
          loader ─┘   └─ resources, anchors      ▼
                                       instance ──► Evaluator ──► Result
                                                     │  ├─ error sink
                                                     │  ├─ annotation sink
                                                     │  └─ classification sink (doc 4)
```

## 2.1 JSON access layer

The core never names `nlohmann::json` directly outside `src/json/`. It uses a small set
of inline functions: kind query, number access (int64/uint64/double), string view,
object lookup and iteration, array size and indexing, deep equality and hashing.

The only backend is `nlohmann::json`. The layer exists to:
- keep the evaluator independent of nlohmann's API details,
- give one place to handle JSON equality, where `1 == 1.0` must be true for
  `enum`, `const` and `uniqueItems` (nlohmann's own `==` already does this),
- leave room for an `ordered_json` or other backend later without rewriting keywords.

## 2.2 URIs

`Uri` is a fresh RFC 3986 implementation: parse, normalize, resolve a reference against
a base, and split off the fragment. Fragments are either a JSON pointer (`#/a/b`) or a
plain name (`#foo`). It gets its own test file, including the RFC 3986 §5.4 examples.
None of this is taken from the predecessor.

## 2.3 Registry

The registry owns every schema document and maps absolute URIs to locations within them.

- **Resource discovery.** Adding a document walks it using the 2020-12 rules for
  which keywords contain subschemas. The superseded `definitions` and `dependencies`
  count too: the spec says to treat `definitions` like `$defs`, and the 2020-12
  meta-schema still describes both. Every `$id` starts an embedded resource. Every
  `$anchor` and `$dynamicAnchor` is recorded against its resource.
- **Base URI.** The caller supplies it, or it comes from the root `$id`. A schema
  with neither gets a generated `urn:uuid:` base so that relative references still
  resolve.
- **Loading.** On a miss, the registry calls
  `schema_loader(const Uri&) -> std::optional<nlohmann::json>`.
  The 2020-12 meta-schemas are built in and never hit the loader.
- **Pointers into unknown keywords.** A `$ref` that points inside an unknown keyword
  or a non-schema location compiles that location on demand, the same way the spec's
  test suite expects.

## 2.4 Compiler

Each schema location becomes a `Node`:

```cpp
struct Node {
    Location            location;      // absolute keyword location (URI + pointer)
    std::uint32_t       id;            // dense index; used for annotation bitsets
    NodeFlags           flags;         // always_true, always_false, needs_annotations, has_name, is_resource_root, ...
    std::vector<KeywordPtr> keywords;  // evaluated in the order below
    const Resource*     resource;      // for dynamic scope
    std::optional<NameId> name;        // classification name (doc 4); set at compile time
};
```

**Keyword order** inside a node is fixed, so that annotations exist before the
keywords that read them:

1. assertions (`type`, `enum`, numeric/string/array/object limits, `required`, ...)
2. `$ref`, `$dynamicRef`
3. in-place applicators: `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else`, `dependentSchemas`
4. child applicators: `properties`, `patternProperties`, `additionalProperties`,
   `prefixItems`, `items`, `contains`, `propertyNames`
5. `unevaluatedProperties`, `unevaluatedItems`

Assertions produce no annotations that other keywords read, so they go first: the
fast path fails on a wrong `type` before descending, and a node's own errors come
before its children's (doc 3.2). Only `unevaluated*` has to be last.

**Keyword values are checked** when compiled (wrong types, negative limits, invalid
regexes, duplicate `type` names), so a malformed schema fails at compile time even
before meta-schema validation exists (milestone 3).

**Reference cycles.** A cycle made only of in-place edges (`$ref`, `allOf`, `anyOf`,
`oneOf`, `not`, `if`/`then`/`else`, `dependentSchemas`) would apply a schema to the
same instance location forever, so the compiler rejects it. `{"$ref": "#"}` is the
simplest case. Cycles through child applicators consume the instance and are fine.
`$dynamicRef` targets depend on the dynamic scope, so its cycles are caught at run
time instead: re-entering the same `$dynamicRef` at the same instance location throws.

**References** are resolved at compile time into `Node*`. Cycles are allowed, because
the graph is not a tree. `$dynamicRef` keeps its initial target and anchor name and
resolves the rest at runtime (2.6).

**Static analysis passes**, run after the graph is built:
- `needs_annotations`: true for a node if annotations from it can reach an
  `unevaluated*` keyword at the same instance location, or if classification is on.
  This spreads backwards through in-place applicators and `$ref`. Any `$dynamicRef`
  is treated as reaching everything, to be safe. Nodes without the flag skip all
  annotation bookkeeping.
- **Name assignment** for classification (doc 4).
- **Regex compilation** for `pattern` and `patternProperties`, so bad patterns fail at
  compile time.

## 2.5 Evaluator

The core function is

```cpp
bool evaluate(const Node&, const Json& instance, Frame& frame, Context& ctx);
```

- `Context` holds the per-validation state: the instance-path stack (segments only,
  turned into a `json_pointer` only when reporting), the dynamic-scope stack, the
  error, annotation and classification sinks, and options.
- `Frame` holds the annotations collected for **one instance location** by the node
  currently being evaluated:
  - `evaluated_props`: a bitset over the instance object's members, by index in
    iteration order,
  - `evaluated_items`: the count of evaluated leading items, plus a bitset for
    items matched by `contains`,
  - pending reports for that location: classifications, `format`, `default`, etc.

**Annotation rules** (the part that matters for `unevaluated*`):
- An in-place applicator evaluates each subschema into a *child frame*. If the
  subschema passes, the child frame is merged into the parent. If it fails, the child
  frame is thrown away.
- `anyOf` evaluates **all** branches when `needs_annotations` is set, and stops at the
  first pass otherwise. `oneOf` always evaluates enough branches to prove exactly one
  passes. `not` never contributes annotations.
- `if` contributes its annotations only when it passes. `then`/`else` contribute when
  they pass.
- Child applicators (`properties` etc.) set bits in the current frame for the
  members/items they cover, whatever the child's result. Child results still decide
  validity.
- If a node fails, the caller discards its frame, which removes everything collected
  under it.
- **Dropped annotations.** When output that reports failed subschemas is requested
  (`verbose`, and the newer output specification's "dropped annotations"; doc 8), a
  failed child frame's annotations move to a dropped list on the parent instead of
  being freed. They never feed `unevaluated*` or classification. Every other path
  frees them as above, so the fast paths pay nothing.

**Errors** follow the same discipline. Errors found inside a branch that may later be
discarded (`anyOf`, `oneOf`, `not`, `if`) are buffered in the child frame. They are
promoted only when the enclosing applicator fails. This is how the final error list
contains only errors on the failing path, with no stray errors from `anyOf` branches
that were superseded.

**Short-circuiting.** When `Context` has no error sink and no annotation consumers,
and the node lacks `needs_annotations`, evaluation returns at the first failure. This
"flag" fast path should be competitive with the fastest validators.

## 2.6 `$dynamicRef`

The context keeps a stack of the schema resources entered on the way to the current
node (pushed when evaluation crosses into a node with `is_resource_root`).

At a `$dynamicRef`:
1. Resolve statically to the initial target.
2. If the initial target is not a `$dynamicAnchor` with the same name, use it as-is
   (2020-12 "bookending" rule).
3. Otherwise walk the dynamic scope from outermost to innermost. Use the first
   resource that defines a `$dynamicAnchor` of that name.

The candidates are found at compile time: for each `$dynamicRef` whose initial target
is a dynamic anchor, the compiler records the node for that anchor name in every
registered resource. At run time the evaluator only walks the scope and looks each
resource up in that list. The scope is tracked only when the schema has such a
`$dynamicRef`.

## 2.7 Memory and threading

- `CompiledSchema` owns the registry and the node graph. Once compiled it cannot be
  changed, and calls to `validate` are `const`, so one compiled schema can be used
  from many threads at once.
- Per-call state (frames, stacks, buffers) is allocated from a small arena that the
  caller can reuse (`ValidationContext`) to avoid allocating on every call.
