// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Validation results: errors, annotations, and the options of schema::validate (doc 3.2).
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <jsonschema/export.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251 4275) // see uri.hpp
#endif

namespace jsonschema {

using json = nlohmann::json;

class classification_sink;

/// One validation error. A regular value type, so errors can be stored, copied and assigned.
///
/// Field names follow the newer JSON Schema output specification (doc 8). In 2020-12's
/// own output format, `evaluation_path` is "keywordLocation" and `schema_location` is
/// "absoluteKeywordLocation".
struct error
{
    json::json_pointer instance_location;   ///< where in the instance the error is
    std::string evaluation_path;            ///< path through the schema, including `$ref` hops, ending in the keyword
    std::string schema_location;            ///< absolute URI of the keyword (`uri#/pointer/keyword`)
    std::string_view keyword;               ///< e.g. "required"; "false" for the `false` schema
    std::string message;                    ///< English, for people
    const json* instance = nullptr;         ///< the failing value; points into the validated instance,
                                            ///< so it is valid as long as the instance is
};

/// Receives errors. Errors are delivered only once their enclosing applicators have decided
/// that they matter: an error in an `anyOf` branch is never delivered if another branch passes.
/// They arrive depth-first, a node's own errors before those of its subschemas, and an
/// applicator's summary error (such as "no subschema of anyOf matched") before its branches'.
class JSONSCHEMA_EXPORT error_sink
{
public:
    virtual ~error_sink() = default;
    virtual void on_error(const error& e) = 0;
};

/// Keeps every error. `error::instance` stays valid while the validated instance lives.
class JSONSCHEMA_EXPORT collecting_error_sink final : public error_sink
{
public:
    void on_error(const error& e) override { errors.push_back(e); }
    std::vector<error> errors;
};

/// Keeps the first error only (pair with `validation_options::max_errors = 1` to stop early).
class JSONSCHEMA_EXPORT first_error_sink final : public error_sink
{
public:
    void on_error(const error& e) override
    {
        if (errors.empty()) {
            errors.push_back(e);
        }
    }
    [[nodiscard]] bool has_error() const noexcept { return !errors.empty(); }
    [[nodiscard]] const error& first() const { return errors.front(); }
    std::vector<error> errors;   ///< at most one element
};

/// Thrown by `throwing_error_sink`.
class JSONSCHEMA_EXPORT validation_error : public std::runtime_error
{
public:
    explicit validation_error(const error& e);

    json::json_pointer instance_location;
    std::string evaluation_path;
    std::string schema_location;
    std::string keyword;
};

/// Throws `validation_error` for the first error delivered.
class JSONSCHEMA_EXPORT throwing_error_sink final : public error_sink
{
public:
    void on_error(const error& e) override;
};

/// One annotation produced by a keyword on a passing path: `title`, `description`,
/// `default`, `deprecated`, `readOnly`, `writeOnly`, `examples`, `format`,
/// `contentEncoding`, `contentMediaType`, `contentSchema`, and unknown keywords, whose
/// values 2020-12 collects as annotations.
struct annotation
{
    json::json_pointer instance_location;
    std::string evaluation_path;            ///< ends in the keyword
    std::string schema_location;            ///< absolute URI of the keyword
    std::string_view keyword;
    const json& value;                      ///< the keyword's value in the schema
};

/// Receives annotations after validation finishes, in evaluation order. Annotations from
/// subschemas that failed, and from `not`, are never delivered, so a failing instance
/// produces none.
class JSONSCHEMA_EXPORT annotation_sink
{
public:
    virtual ~annotation_sink() = default;
    virtual void on_annotation(const annotation& a) = 0;
};

struct validation_options
{
    error_sink* errors = nullptr;                       ///< nullptr: errors are not reported
    annotation_sink* annotations = nullptr;             ///< nullptr: annotations are not reported
    classification_sink* classifications = nullptr;     ///< doc 4; needs classification enabled at compile time
    std::size_t max_errors = 0;                         ///< 0 = unlimited. Limits delivery only; with a
                                                        ///< classification sink the walk still completes.
    bool check_all_matches = false;                     ///< With a classification sink, examine all alternatives and contains items
                                                        ///< even when validation can already determine the outcome.
};

/// The standard output formats of JSON Schema 2020-12 §12.4, produced by `schema::output`.
///
/// `verbose` is not offered: it reports every subschema evaluated, including passing ones
/// without annotations, which the evaluator does not record (doc 3.2).
enum class output_format
{
    flag,       ///< `{"valid": true}` only
    basic,      ///< a flat list of error units (or, when valid, annotation units)
    detailed    ///< the same units nested by schema structure, single-child levels collapsed
};

struct result
{
    bool valid = false;
    explicit operator bool() const noexcept { return valid; }
};

} // namespace jsonschema

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
