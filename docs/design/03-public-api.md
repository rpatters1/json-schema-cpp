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
    format_mode               formats = format_mode::annotate;   // annotate | assert
    bool                      accept_other_dialects = false;     // treat foreign $schema as 2020-12
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

    void add_resource(const json& doc, std::string_view uri);        // before compile only; see registry
    const uri& base_uri() const;
};

class registry {                        // optional: pre-load several documents, then compile from it
public:
    void add(const json& doc, std::optional<std::string_view> uri = {});
    schema compile(std::string_view root_uri, compile_options = {}) &&;
};

}
```

`schema_error` derives from `std::runtime_error`. It carries the schema location and,
when meta-schema validation failed, the list of meta-schema errors.

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
    output validate(const json& instance, output_format) const;        // flag | basic | detailed | verbose
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
    std::string        keyword_location;           // path through the schema, including $ref hops
    std::string        absolute_keyword_location;  // URI of the keyword
    std::string_view   keyword;                    // e.g. "required"
    std::string        message;                    // human-readable
    const json&        instance;                   // the failing value; valid during the callback
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
`throwing_error_sink`.

### Standard output

`output_format::{flag, basic, detailed, verbose}` produce the JSON structures defined
in 2020-12 §12 as `nlohmann::json`. They are built on the same sinks, so they add
nothing to the core.

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
