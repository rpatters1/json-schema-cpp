// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Custom keywords (design doc 3.5): a small extension point for keywords a schema invents.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <jsonschema/export.hpp>

namespace jsonschema {

using json = nlohmann::json;

namespace detail {
class compile_scope;
class eval_context;
class extension_keyword;
struct extension_state;
struct node;
} // namespace detail

/// A subschema that a custom keyword compiled, for applying with `keyword_eval_scope`.
/// A small value type; copy it into the `custom_keyword`.
class subschema
{
public:
    subschema() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return node_ != nullptr; }

private:
    friend class keyword_compile_scope;
    friend class keyword_eval_scope;
    subschema(const detail::node* n, const std::string* path, bool in_place) noexcept : node_(n), path_(path), in_place_(in_place) {}
    const detail::node* node_ = nullptr;
    const std::string* path_ = nullptr;     ///< JSON pointer from the keyword to the subschema
    bool in_place_ = false;
};

/// What a custom keyword's `compile` function sees: the keyword's value and where it is.
class JSONSCHEMA_EXPORT keyword_compile_scope
{
public:
    /// The keyword's value.
    [[nodiscard]] const json& value() const noexcept;
    /// The schema object that contains the keyword.
    [[nodiscard]] const json& schema() const noexcept;
    /// The absolute keyword location (a URI with a JSON-pointer fragment).
    [[nodiscard]] std::string location() const;
    /// Rejects the schema: throws `schema_error` with @p message and the keyword location.
    /// Any other exception thrown by `compile` is reported the same way.
    [[noreturn]] void fail(std::string_view message) const;

    /// Compiles @p value, which must be part of `value()` (taken by reference from it), as a
    /// subschema the keyword applies to the same instance, as `allOf` does.
    [[nodiscard]] subschema in_place_subschema(const json& value) const;
    /// Compiles @p value, part of `value()`, as a subschema the keyword applies to members or
    /// items of the instance, as `properties` and `items` do. Only these may lead back to the
    /// same schema without consuming input, so recursive structures use them.
    [[nodiscard]] subschema child_subschema(const json& value) const;

    /// Internal: constructed by the library.
    keyword_compile_scope(const detail::compile_scope& scope, detail::extension_state& state) noexcept
        : scope_(scope), state_(state)
    {
    }
    keyword_compile_scope(const keyword_compile_scope&) = delete;
    keyword_compile_scope& operator=(const keyword_compile_scope&) = delete;

private:
    [[nodiscard]] subschema compile(const json& value, bool in_place) const;
    const detail::compile_scope& scope_;
    detail::extension_state& state_;
};

/// What a custom keyword's `evaluate` sees while validating one instance.
class JSONSCHEMA_EXPORT keyword_eval_scope
{
public:
    /// The instance being evaluated (the same as `evaluate`'s argument).
    [[nodiscard]] const json& instance() const noexcept { return instance_; }

    /// True if errors are being reported. When false, `fail` does nothing, so a keyword can
    /// skip building messages.
    [[nodiscard]] bool reporting_errors() const noexcept;
    /// Reports an error at the current instance location. `evaluate` must still return false.
    void fail(std::string message);

    /// Applies @p s to the instance, which must be an `in_place_subschema`. Its errors and
    /// annotations are reported as any subschema's are. Returns whether it passed.
    bool apply(const subschema& s);
    /// Applies @p s to the member @p property of the object instance (std::invalid_argument
    /// if there is no such member).
    bool apply(const subschema& s, std::string_view property);
    /// Applies @p s to the item @p index of the array instance (std::invalid_argument if out of range).
    bool apply(const subschema& s, std::size_t index);

    /// As `apply`, but only tests: nothing is reported, whatever the result. For keywords
    /// whose outcome depends on which subschemas pass, as `anyOf` and `if` do.
    [[nodiscard]] bool test(const subschema& s);
    [[nodiscard]] bool test(const subschema& s, std::string_view property);
    [[nodiscard]] bool test(const subschema& s, std::size_t index);

    /// Internal: constructed by the library.
    keyword_eval_scope(detail::eval_context& context, const detail::extension_keyword& keyword, const json& instance) noexcept
        : context_(context), keyword_(keyword), instance_(instance)
    {
    }
    keyword_eval_scope(const keyword_eval_scope&) = delete;
    keyword_eval_scope& operator=(const keyword_eval_scope&) = delete;

private:
    bool run(const subschema& s, const json* child, std::string_view key, std::size_t index, bool report);
    detail::eval_context& context_;
    const detail::extension_keyword& keyword_;
    const json& instance_;
};

/// One compiled occurrence of a custom keyword. Compiled schemas are shared between threads,
/// so `evaluate` must be safe to call concurrently.
class custom_keyword
{
public:
    custom_keyword() = default;
    custom_keyword(const custom_keyword&) = delete;
    custom_keyword& operator=(const custom_keyword&) = delete;
    virtual ~custom_keyword() = default;

    /// True if @p instance satisfies the keyword. Return true for instance types the keyword
    /// does not apply to. Report why it failed with `scope.fail` (or by `scope.apply`ing a
    /// subschema that fails).
    [[nodiscard]] virtual bool evaluate(const json& instance, keyword_eval_scope& scope) const = 0;
};

/// A custom keyword, registered through `compile_options::extra_keywords`.
///
/// The keyword's value is still reported as an annotation, as for any keyword the library
/// does not know. Its subschemas do not count as evaluating properties or items for
/// `unevaluatedProperties` and `unevaluatedItems`.
struct keyword_extension
{
    /// The keyword name. It must not be a keyword of JSON Schema 2020-12.
    std::string name{};
    /// Empty: the keyword applies in every schema. Otherwise a vocabulary URI: the keyword
    /// applies only in schemas whose meta-schema lists it in `$vocabulary`, and such a
    /// meta-schema may mark the vocabulary as required.
    std::string vocabulary{};
    /// Called once for each occurrence of the keyword, in a schema where it applies. May
    /// return nullptr if this occurrence needs no evaluation. Rejects a bad value with
    /// `scope.fail` (or any exception).
    std::function<std::unique_ptr<custom_keyword>(const keyword_compile_scope& scope)> compile{};
};

} // namespace jsonschema
