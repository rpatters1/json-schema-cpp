// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <jsonschema/export.hpp>
#include <jsonschema/uri.hpp>

namespace jsonschema {

using json = nlohmann::json;

/// Thrown when a schema cannot be compiled: invalid schema, unresolvable reference,
/// unsupported dialect, or a loader failure.
class JSONSCHEMA_EXPORT schema_error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Fetches a schema document that is not otherwise known. Returns `std::nullopt` if the
/// URI cannot be loaded. The argument has no fragment.
using schema_loader = std::function<std::optional<json>(const uri&)>;

/// How the `format` keyword behaves (doc 5.1).
enum class format_mode
{
    annotate,   ///< `format` produces an annotation only (the 2020-12 default)
    assert      ///< `format` is also an assertion
};

/// A schema document made available to `$ref` resolution under a retrieval URI.
struct resource
{
    std::string uri;    ///< absolute retrieval URI
    json document;
};

struct compile_options
{
    std::optional<std::string> base_uri;        ///< default: the root `$id`, else a generated `urn:uuid:` URI
    schema_loader loader;                       ///< consulted for unknown URIs; may be empty
    std::vector<resource> resources;            ///< documents to register before compiling
    format_mode formats = format_mode::annotate;
    bool accept_other_dialects = false;         ///< treat a non-2020-12 `$schema` as 2020-12
};

namespace detail { class compiled_schema; }

/// A compiled, immutable schema. Validation is `const` and thread-safe.
///
/// Milestone 0: this is a placeholder so that the test-suite runner can be built.
/// `compile` throws `schema_error` until milestone 1.
class JSONSCHEMA_EXPORT schema
{
public:
    [[nodiscard]] static schema compile(const json& root, compile_options options = {});

    /// True if @p instance is valid against this schema.
    [[nodiscard]] bool is_valid(const json& instance) const;

    schema(schema&&) noexcept;
    schema& operator=(schema&&) noexcept;
    ~schema();

private:
    explicit schema(std::unique_ptr<detail::compiled_schema> impl);
    std::unique_ptr<detail::compiled_schema> impl_;
};

} // namespace jsonschema
