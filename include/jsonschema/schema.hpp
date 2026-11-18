// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <jsonschema/classification.hpp>
#include <jsonschema/custom_keyword.hpp>
#include <jsonschema/export.hpp>
#include <jsonschema/uri.hpp>
#include <jsonschema/validation.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251 4275) // see uri.hpp
#endif

namespace jsonschema {

using json = nlohmann::json;

/// Thrown when a schema cannot be compiled: invalid schema, unresolvable reference,
/// unsupported dialect, or a loader failure.
class JSONSCHEMA_EXPORT schema_error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
    schema_error(const std::string& message, std::vector<error> metaschema_errors);

    /// When a schema document failed validation against its meta-schema: the errors.
    /// Instance locations are locations in the schema document. `error::instance` is null,
    /// because the document does not outlive the exception.
    [[nodiscard]] std::span<const error> metaschema_errors() const noexcept;

private:
    std::shared_ptr<const std::vector<error>> metaschema_errors_;   // shared: keeps copying noexcept
};

/// Thrown during validation when evaluation can produce neither "valid" nor "invalid"
/// (the spec's indeterminate result). Today the only case is a `$dynamicRef` that recurses
/// forever at one instance location, which depends on the dynamic scope and so cannot always
/// be detected when compiling.
class JSONSCHEMA_EXPORT evaluation_error : public std::runtime_error
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

/// A custom `format` checker (doc 3.3): true if @p instance is valid for the format. Called
/// for every instance type; built-in formats accept non-strings, so a checker usually does too.
using format_checker = std::function<bool(const json& instance)>;

/// How the `content*` keywords behave (doc 5.5).
enum class content_mode
{
    annotate,   ///< annotations only (the 2020-12 default)
    assert      ///< also check `contentEncoding`, `contentMediaType` and `contentSchema`
};

/// Decodes a string in a `contentEncoding` (doc 3.3): the decoded bytes, or `std::nullopt`
/// if the string is not valid in the encoding.
using content_decoder = std::function<std::optional<std::string>(std::string_view encoded)>;

/// Parses content of a `contentMediaType` (doc 3.3): `std::nullopt` if the content is not
/// valid for the media type, else the value `contentSchema` validates. A media type with no
/// JSON form can return any value, such as `json()`, and is not used with `contentSchema`.
using content_parser = std::function<std::optional<json>(std::string_view content)>;

/// A schema document made available to `$ref` resolution under a retrieval URI.
struct resource
{
    std::string uri{};      ///< absolute retrieval URI
    json document{};
};

/// Every member has a default member initializer, so designated initializers that set only
/// some members (`{.base_uri = "..."}`) compile cleanly under -Wmissing-field-initializers.
struct compile_options
{
    std::optional<std::string> base_uri{};      ///< default: the root `$id`, else a generated `urn:uuid:` URI
    schema_loader loader{};                     ///< consulted for unknown URIs; may be empty
    std::vector<resource> resources{};          ///< documents to register before compiling
    /// `annotate` (the 2020-12 default) or `assert`. `format` also asserts when the schema's
    /// meta-schema lists the format-assertion vocabulary.
    format_mode formats = format_mode::annotate;
    /// Checkers for additional format names, or replacements for built-in ones (by name).
    std::vector<std::pair<std::string, format_checker>> extra_formats{};
    /// `annotate` (the 2020-12 default) or `assert`. Assertion checks known encodings and
    /// media types only: built in are `base64`, `base64url`, `base32`, `base16`, and
    /// `application/json` and other `+json` types.
    content_mode content = content_mode::annotate;
    /// Decoders for additional `contentEncoding` names, or replacements for built-in ones.
    /// Names match ignoring case.
    std::vector<std::pair<std::string, content_decoder>> extra_content_encodings{};
    /// Parsers for additional `contentMediaType` values, or replacements for built-in ones.
    /// Matched ignoring case and parameters (`; charset=...`).
    std::vector<std::pair<std::string, content_parser>> extra_media_types{};
    /// Custom keywords (doc 3.5).
    std::vector<keyword_extension> extra_keywords{};
    bool accept_other_dialects = false;         ///< treat a non-2020-12 `$schema` as 2020-12
    /// Validate each schema document against its meta-schema before using it (doc 3.1).
    bool validate_against_metaschema = true;
    /// Evaluate the superseded draft-07 `dependencies` keyword, as `dependentRequired` for
    /// array members and `dependentSchemas` for schema members. 2020-12 no longer defines it,
    /// but its meta-schema still describes it, and a schema that uses it almost always means
    /// it. Set to false for strict 2020-12 behavior, where it is ignored like any unknown keyword.
    bool legacy_dependencies = true;
    classification_options classification{};    ///< doc 4; off by default
};

namespace detail { class compiled_schema; }

/// A compiled, immutable JSON Schema 2020-12 schema. Validation is `const`, so one
/// compiled schema can be used from many threads at once. A moved-from `schema` may only
/// be assigned to or destroyed.
class JSONSCHEMA_EXPORT schema
{
public:
    /// Compiles @p root, resolving every reference it can reach. Throws `schema_error` if
    /// the schema is invalid, uses another dialect, or has an unresolvable reference.
    [[nodiscard]] static schema compile(const json& root, compile_options options = {});
    [[nodiscard]] static schema compile(json&& root, compile_options options = {});

    /// True if @p instance is valid against this schema. Stops at the first failure.
    ///
    /// Throws `evaluation_error` if the result is indeterminate (see `evaluation_error`).
    [[nodiscard]] bool is_valid(const json& instance) const;

    /// Validates @p instance, reporting to the sinks in @p options. Without sinks this is
    /// equivalent to `is_valid`. Throws `evaluation_error` like `is_valid`, and
    /// `std::invalid_argument` if a classification sink is given but classification was not
    /// enabled at compile time.
    [[nodiscard]] result validate(const json& instance, const validation_options& options = {}) const;

    /// Validates @p instance and returns the result in a standard output format (2020-12 §12.4):
    /// `valid`, then `errors` (or `annotations` when valid) as output units with
    /// `keywordLocation`, `absoluteKeywordLocation`, `instanceLocation`, and `error` or
    /// `annotation`. Throws `evaluation_error` like `is_valid`.
    [[nodiscard]] json output(const json& instance, output_format format = output_format::basic) const;

    /// The base URI of the root schema: its `$id`, else `compile_options::base_uri`, else a
    /// generated `urn:uuid:` URI.
    [[nodiscard]] const uri& base_uri() const noexcept;

    schema(schema&&) noexcept;
    schema& operator=(schema&&) noexcept;
    ~schema();

private:
    explicit schema(std::unique_ptr<detail::compiled_schema> impl);
    std::unique_ptr<detail::compiled_schema> impl_;
};

} // namespace jsonschema

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
