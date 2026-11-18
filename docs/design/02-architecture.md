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

**Passes run after the graph is built:**
- **Name assignment** for classification (doc 4), and each named node's `schema_info`.
- **Reference-cycle rejection** (above).

There is no static `needs_annotations` pass. Whether annotations are needed is decided
at run time instead: a frame is passed down only where something will read it (2.5).
That is exact, including through `$dynamicRef`, and costs nothing where no frame is
passed.
- **Regex compilation** for `pattern` and `patternProperties`, so bad patterns fail at
  compile time.

## 2.5 Evaluator

The core function is

```cpp
bool evaluate(const node&, const json& instance, eval_context&, frame* annotations, edge entered_by);
```

- `eval_context` holds the per-validation state:
  - the instance-location and evaluation-path stacks, kept only when something will
    report them;
  - the dynamic-scope stack;
  - three **logs**: errors, value annotations, and classification matches;
  - the options.
- `frame` holds what `unevaluated*` needs for **one instance location**:
  - a bitset of evaluated object members, by index in iteration order;
  - the count of evaluated leading items, an "all items" flag, and a bitset for items
    matched by `contains`.

  A node with `unevaluated*` gets a frame of its own. Every other node writes into
  the frame its caller passed, or does no annotation work when that is `nullptr`.
- `edge` records how evaluation entered the node (root, child, `$ref`, in-place). It
  is used for classification's `via` (doc 4.3).

**Annotation rules** (the part that matters for `unevaluated*`):
- Unconditional in-place applicators (`$ref`, `allOf`, `then`/`else`,
  `dependentSchemas`) pass the caller's frame straight through. Their failure fails
  the node, and the caller then discards that frame anyway.
- Conditional ones (`anyOf`, `oneOf`, `if`) evaluate each branch into a scratch frame,
  which is merged only if the branch passes.
- `anyOf` evaluates **all** branches when a frame was passed or annotations or
  classifications are being collected, and stops at the first pass otherwise. `oneOf`
  evaluates enough branches to prove that exactly one passes. `not` never contributes
  annotations.
- `if` contributes its annotations only when it passes. `then`/`else` contribute when
  they pass.
- Child applicators (`properties` etc.) set bits in the current frame for the
  members/items they cover, whatever the child's result. Child results still decide
  validity.
- If a node fails, the caller discards its frame, which removes everything collected
  under it.
- The value-annotation and classification logs follow the same rule by position. A
  node remembers each log's size when it starts. If it fails, it truncates the
  annotation log back to that size. It truncates the classification log too, unless
  the failure was a content failure under recovery (doc 4.6). Conditional applicators
  also erase the windows of branches that didn't count.
- **Dropped annotations.** When output that reports failed subschemas is requested
  (`verbose`, and the newer output specification's "dropped annotations"; doc 8), a
  failed child frame's annotations move to a dropped list on the parent instead of
  being freed. They never feed `unevaluated*` or classification. Every other path
  frees them as above, so the fast paths pay nothing.

**Errors** follow the same discipline.
- Outside any conditional applicator, an error is delivered to the sink at once: on
  that path it decides the result.
- Inside one (`anyOf`, `oneOf`, `not`, `if`, `contains`), errors are buffered from a
  mark. When the applicator passes, they are dropped. When it fails, they stay, behind
  a summary error inserted at the mark, so a parent's error comes before its
  children's.
- Conditional branches are first evaluated **with errors off**. Only when an `anyOf`
  or `oneOf` fails are its branches re-run to collect their errors. `not`, `if` and
  `contains` never report their branches' errors. Valid documents therefore never
  build error text that is thrown away. On the MNX examples, errors plus
  classification cost the same as classification alone, and errors alone cost the
  same as `is_valid`.

**Short-circuiting.** With no sinks and no frame, a node returns at its first failing
keyword. That is the "flag" fast path used by `is_valid`. With an error sink, every
keyword runs so that every error is found, until `max_errors` is reached. With a
classification sink, everything is walked.

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
