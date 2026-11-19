# 3. Public API

Namespace: `jsonschema`. Header: `<jsonschema/jsonschema.hpp>`. Naming is snake_case
to match `nlohmann::json` and the standard library (decided).

Public headers are C++20. `span<T>` in signatures is `std::span<T>`, and options
structs are designed to be filled with designated initializers (see doc 1,
*Language standard*).

## 3.1 Compiling

```cpp
namespace jsonschema {

using json = nlohmann::json;

struct compile_options {
    std::optional<std::string> base_uri;          // default: root $id, else generated urn:uuid
    schema_loader             loader;              // std::function<std::optional<json>(const uri&)>
    std::vector<resource>     resources;           // {retrieval uri, document} pairs registered before compiling
    format_mode               formats = format_mode::annotate;   // annotate | assert
    std::vector<std::pair<std::string, format_checker>> extra_formats;          // name -> checker (3.3)
    content_mode              content = content_mode::annotate;  // annotate | assert (3.3)
    std::vector<std::pair<std::string, content_decoder>> extra_content_encodings;   // 3.3
    std::vector<std::pair<std::string, content_parser>>  extra_media_types;         // 3.3
    std::vector<keyword_extension>     extra_keywords;   // custom keywords (3.5)
    bool                      accept_other_dialects = false;     // treat foreign $schema as 2020-12
    bool                      validate_against_metaschema = true;
    bool                      legacy_dependencies = true;        // evaluate draft-07 `dependencies` (doc 1)
    classification_options    classification;      // doc 4; off by default
};

class schema {                          // immutable, thread-safe, cheap to move
public:
    static schema compile(const json& root, compile_options = {});   // throws schema_error
    static schema compile(json&& root, compile_options = {});

    const uri& base_uri() const;
};

}
```

Extra documents are supplied as `compile_options::resources`, and anything else is
fetched through `loader`. A separate public `registry` class (for sharing pre-loaded
documents between several compiled schemas) was considered and deferred until there's
a need for it.

`schema_error` derives from `std::runtime_error`. It carries the schema location and,
when meta-schema validation failed, the list of meta-schema errors. They are returned by
`metaschema_errors()`, located in the schema document, with a null `instance`.

**Meta-schema validation** (`validate_against_metaschema`, on by default):
- Each schema document is validated against its meta-schema when its first node is
  compiled, so its errors come before the compiler's own checks. Documents that are
  only registered, and never referenced, are not validated.
- The 2020-12 meta-schema is compiled once per process and shared. A custom meta-schema
  is compiled into the schema being built.
- Documents of other drafts, accepted with `accept_other_dialects`, are not validated.
- The compiler's own checks of keyword values remain for when validation is off.

`evaluation_error`, also derived from `std::runtime_error`, is thrown *during
validation* when the result is indeterminate: neither valid nor invalid. Today the only
case is a `$dynamicRef` that recurses forever at one instance location. It is separate
from `schema_error` so that callers of `is_valid` and `validate` can tell a broken
evaluation from a broken schema. JSON Schema v1 adds more such cases (doc 8).

## 3.2 Validating

```cpp
struct validation_options {
    error_sink*          errors = nullptr;          // nullptr: only valid/invalid is needed
    annotation_sink*     annotations = nullptr;     // all annotations, at their instance locations
    classification_sink* classifications = nullptr; // doc 4
    std::size_t          max_errors = 0;            // 0 = unlimited; limits delivery only,
                                                    // never ends a classification walk early (doc 4.6)
    bool check_all_matches = false;                 // collect optional classification alternatives
};

class schema {
    // ...
    bool   is_valid(const json& instance) const;                       // fastest path
    result validate(const json& instance, const validation_options& = {}) const;
    json   output(const json& instance, output_format = output_format::basic) const;   // see "Standard output"
};

struct result {
    bool valid;
    explicit operator bool() const { return valid; }
};
```

### Error sink

```cpp
struct error {
    json::json_pointer instance_location;
    std::string        evaluation_path;    // path through the schema, including $ref hops
                                           //   (2020-12 output: "keywordLocation")
    std::string        schema_location;    // absolute URI of the keyword
                                           //   (2020-12 output: "absoluteKeywordLocation")
    std::string_view   keyword;            // e.g. "required"
    std::string        message;            // human-readable
    const json*        instance;           // the failing value; points into the validated instance
};

class error_sink {
public:
    virtual ~error_sink() = default;
    virtual void on_error(const error&) = 0;
};
```

Errors are delivered after their enclosing applicator's outcome is known (doc 2.5),
so the sink never sees errors that a passing `anyOf` branch later made irrelevant.
They are delivered depth-first, parents before children, so a consumer can keep only
the topmost error per instance subtree. That is what mnxdom's current `errorHandler`
does by hand.

Ready-made sinks: `collecting_error_sink` (a vector), `first_error_sink`, and
`throwing_error_sink`, which throws `validation_error` carrying the error's locations.

`error` is a regular value type, so errors can be stored and assigned: an exported
`collecting_error_sink` needs that in Windows DLL builds. `error::instance` is a pointer
for the same reason, and it always points into the validated instance. A `propertyNames` failure
is therefore reported on the object, with the property name and the inner reason in
the message, because the name itself isn't part of the instance.

### Annotation sink

`annotation_sink::on_annotation(const annotation&)` receives, after validation, every
value annotation on a passing path: the meta-data, `format` and `content*` keywords,
and unknown keywords. Each comes with its instance location, evaluation path and
absolute schema location. The `content*` keywords annotate strings only, and
`contentSchema` only alongside `contentMediaType`. The official annotation test suite
runs in CI (doc 6).

### Standard output

`schema::output(instance, output_format)` returns the structures of 2020-12 §12.4 as
`nlohmann::json`. It was added in milestone 3b.

- **`flag`**: `{"valid": …}` only. It uses the `is_valid` fast path.
- **`basic`**: `valid`, then a flat `errors` list (or `annotations` when valid) of output
  units. Each unit has `keywordLocation`, `absoluteKeywordLocation`, `instanceLocation`
  and `error` (or `annotation`).
- **`detailed`**: the same units, nested by the subschema evaluations that enclose them.
  - A level with a single entry is replaced by that entry, so for the polygon example of
    §12.5 the output matches the spec's structure.
  - The nesting comes from a chain of open subschema evaluations, which the evaluator
    records only for this format.
- **`verbose`** is not offered. It lists every subschema evaluated, including passing
  ones that have no annotations, and the evaluator records only errors and annotations.
  Open question 15.

Units are in evaluation order, so a schema's own assertions come before its subschemas'
(doc 2.4). §12 doesn't make the order significant. Annotations are the value annotations
of the annotation sink. The applicators' own annotations, such as the property names that
`properties` evaluated, are internal to `unevaluated*` and are not reported.

Field and member names inside the library follow the terms of the newer output
specification (`evaluation_path`, `schema_location`; doc 8). The §12 serializers map
them to 2020-12's JSON names. A later version can then add that specification's `list`
and `hierarchical` formats without renaming anything. `verbose` needs annotations from
failed subschemas, so the evaluator can keep them on request (doc 2.5).

## 3.3 Formats and content

```cpp
using format_checker  = std::function<bool(const json& instance)>;    // false: invalid; sees every type
using content_decoder = std::function<std::optional<std::string>(std::string_view encoded)>;   // nullopt: invalid
using content_parser  = std::function<std::optional<json>(std::string_view content)>;          // nullopt: invalid
```

Built-in checkers cover every format defined in 2020-12 (doc 5.1). Callers can override
any built-in or add new ones.

The `content*` keywords are annotations unless `content = content_mode::assert` (doc 5.5).
A decoder turns a string in a `contentEncoding` into bytes. A parser checks bytes against a
`contentMediaType` and returns the value `contentSchema` validates. Built in are the RFC 4648
encodings and JSON media types, and callers can add or replace either. Doc 3.3 first
sketched one `content_checker` callback. It was split into a decoder and a parser
(milestone 4d) because `contentSchema` needs the decoded, parsed value, and the two halves
combine freely: `base64` with any media type.

## 3.4 Reusable context

```cpp
class validation_context;   // holds per-call buffers; not thread-safe; reuse across calls on one thread
result validate(const json&, const validation_options&, validation_context&) const;
```

Not implemented yet. A benchmark should show that per-call allocation matters before
it is added.

## 3.5 Custom keywords

A deliberately small extension point (milestone 4d), in `<jsonschema/custom_keyword.hpp>`:

```cpp
struct keyword_extension {
    std::string name;            // not a 2020-12 keyword
    std::string vocabulary;      // optional URI; ties the keyword to $vocabulary
    std::function<std::unique_ptr<custom_keyword>(const keyword_compile_scope&)> compile;
};

class custom_keyword {           // one compiled occurrence; evaluate is called concurrently
public:
    virtual ~custom_keyword() = default;
    virtual bool evaluate(const json& instance, keyword_eval_scope& scope) const = 0;
};
```

- **Compiling.** `compile` runs once for each occurrence. `keyword_compile_scope` gives
  the keyword's `value()`, the enclosing `schema()`, the `location()`, and `fail(message)`.
  Any exception from `compile` becomes a `schema_error` at the keyword's location. It may
  return nullptr when an occurrence needs no evaluation.
- **Subschemas.** `in_place_subschema(v)` and `child_subschema(v)` compile a part of the
  keyword's value as a schema and return a `subschema` handle.
  - The two kinds tell the compiler how the keyword applies them. In-place subschemas join
    the infinite-recursion check, as `allOf` does. Child subschemas apply to members or
    items, so recursive structures through them are fine.
  - Subschemas are discovered when compiled, so `$ref` into them and `$id`/`$anchor`
    inside them work.
- **Evaluating.** `keyword_eval_scope` has these operations:
  - `fail(message)` reports an error at the current instance location. `reporting_errors()`
    says whether that will be used.
  - `apply(s)`, `apply(s, property)` and `apply(s, index)` evaluate a subschema on the
    instance or on one of its members or items. Their errors and annotations are reported
    like any subschema's. The evaluation path is the keyword followed by the subschema's
    pointer inside the keyword's value, such as `/both/0/type`.
  - `test(...)` does the same, but reports nothing, for keywords that choose between
    alternatives as `anyOf` and `if` do.
- **Annotations.** The keyword's value is still reported as an annotation, as for any
  keyword the library does not define. A custom keyword cannot produce other annotation
  values.
- **`unevaluated*`.** Subschemas a custom keyword applies do not mark properties or items
  as evaluated for `unevaluatedProperties` and `unevaluatedItems`. That needs access to the
  evaluator's annotation frames, and can be added later without changing this API.
- **Vocabularies.** With a `vocabulary` URI, the keyword applies only where the schema's
  meta-schema lists that vocabulary in `$vocabulary`. Registering it also lets a
  meta-schema require it, where an unknown required vocabulary is otherwise refused.
  Without one, the keyword applies everywhere.
- **Names.** Registration rejects these at `compile` time:
  - empty names, and names registered twice;
  - 2020-12 keyword names, including the meta-data, `content*` and superseded keywords;
  - a 2020-12 vocabulary URI.

The MNX schema's own `glyph` keyword is an example. It is plain metadata today, but an MNX
tool could register a checker for it.

## 3.6 Migration from pboettch/json-schema-validator

| old | new |
|-----|-----|
| `json_validator v; v.set_root_schema(s);` | `auto v = jsonschema::schema::compile(s);` |
| `v.validate(doc)` (throws) | `v.validate(doc, {.errors = &throwing_error_sink})` |
| `v.validate(doc, handler)` | `v.validate(doc, {.errors = &sink})` |
| `error_handler::error(ptr, instance, msg)` | `error_sink::on_error(const error&)` |
| `schema_loader(uri, json&)` | `schema_loader(uri) -> optional<json>` |
| default-value patch return value | removed. Use `default` annotations |

No compatibility layer for the old `nlohmann::json_schema::json_validator` API is
provided (decided). This table is migration guidance only.
