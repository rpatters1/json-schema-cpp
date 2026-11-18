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
    bool                      accept_other_dialects = false;     // treat foreign $schema as 2020-12
    bool                      legacy_dependencies = true;        // evaluate draft-07 `dependencies` (doc 1)
    bool                      validate_against_metaschema = true;
    regex_engine_factory      regex;               // default: built-in engine (doc 5)
    classification_options    classification;      // doc 4; off by default
    std::vector<format_checker_entry>  extra_formats;    // name -> checker
    std::vector<keyword_extension>     extra_keywords;   // custom keywords (3.5)
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
};

class schema {
    // ...
    bool   is_valid(const json& instance) const;                       // fastest path
    result validate(const json& instance, const validation_options& = {}) const;
    output validate(const json& instance, output_format) const;        // see "Standard output"
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

`output_format::{flag, basic, detailed, verbose}` (milestone 3) produce the JSON
structures defined in 2020-12 §12 as `nlohmann::json`. They are built on the same sinks, so they add
nothing to the core.

Field and member names inside the library follow the terms of the newer output
specification (`evaluation_path`, `schema_location`; doc 8). The §12 serializers map
them to 2020-12's JSON names. A later version can then add that specification's `list`
and `hierarchical` formats without renaming anything. `verbose` needs annotations from
failed subschemas, so the evaluator can keep them on request (doc 2.5).

## 3.3 Formats and content

```cpp
using format_checker  = std::function<bool(std::string_view value)>;   // false: invalid
using content_checker = std::function<std::optional<std::string>(       // error message or nullopt
    std::string_view encoding, std::string_view media_type, const json& instance)>;
```

Built-in checkers cover every format defined in 2020-12 (doc 5). Callers can override
any built-in or add new ones.

## 3.4 Reusable context

```cpp
class validation_context;   // holds per-call buffers; not thread-safe; reuse across calls on one thread
result validate(const json&, const validation_options&, validation_context&) const;
```

Not implemented yet. A benchmark should show that per-call allocation matters before
it is added.

## 3.5 Custom keywords

A deliberately small extension point. Its exact shape is settled in milestone 4.

```cpp
struct keyword_extension {
    std::string name;
    std::string vocabulary_uri;      // optional; ties the keyword to $vocabulary
    // compile: called once with the keyword's value; returns an evaluator
    std::function<std::unique_ptr<custom_keyword>(const json& value, compile_scope&)> compile;
};

class custom_keyword {
public:
    virtual ~custom_keyword() = default;
    virtual bool evaluate(const json& instance, eval_scope&) const = 0;   // may report errors/annotations via scope
};
```

The MNX schema's own `glyph` keyword is an example. It is plain metadata today, but
an MNX tool could register a checker for it.

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
