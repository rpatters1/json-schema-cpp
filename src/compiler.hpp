// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The schema compiler (design doc 2.4): turns registered schema documents into an
// immutable graph of nodes.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <jsonschema/schema.hpp>

#include "node.hpp"
#include "regex.hpp"
#include "registry.hpp"

namespace jsonschema::detail {

class compiler;

/// What a `$dynamicRef` may resolve to at runtime (doc 2.6): for every resource that
/// defines a `$dynamicAnchor` of the given name, the node it names. Filled in by the
/// compiler after the rest of the graph is built.
struct dynamic_anchor_binding
{
    std::string anchor;
    std::vector<std::pair<const schema_resource*, const node*>> candidates;
};

/// A compiled schema and everything it owns. Immutable once compiled.
class compiled_schema
{
public:
    explicit compiled_schema(schema_loader loader) : schemas(std::move(loader)) {}

    registry schemas;
    std::deque<node> nodes;
    const node* root = nullptr;
    uri base_uri;
    bool uses_dynamic_scope = false;    ///< some `$dynamicRef` needs the dynamic scope
    bool classification_enabled = false;
    bool recover = true;
};

/// What a keyword factory sees while compiling one keyword.
class compile_scope
{
public:
    compile_scope(compiler& owner, const node& schema_node, std::string_view keyword_name, const json& keyword_value) noexcept
        : owner_(owner), node_(schema_node), keyword_(keyword_name), value_(keyword_value)
    {
    }

    /// The keyword's value.
    [[nodiscard]] const json& value() const noexcept { return value_; }
    /// The schema object that contains the keyword.
    [[nodiscard]] const json& schema() const noexcept { return *node_.schema; }
    /// A scope for a sibling keyword that this keyword compiles (e.g. `minContains` for
    /// `contains`), so that failures report the sibling's location. @p name must outlive it.
    [[nodiscard]] compile_scope for_sibling(std::string_view name, const json& value) const noexcept
    {
        return compile_scope(owner_, node_, name, value);
    }

    /// A sibling keyword's value, or nullptr.
    [[nodiscard]] const json* sibling(std::string_view name) const;
    [[nodiscard]] std::string_view keyword_name() const noexcept { return keyword_; }
    [[nodiscard]] const node& current() const noexcept { return node_; }
    [[nodiscard]] const compile_options& options() const noexcept;

    /// Compiles @p value, a subschema of this keyword. Fails unless it is an object or a boolean.
    [[nodiscard]] const node* subschema(const json& value) const;

    /// Resolves a URI reference against the current base URI and compiles its target.
    struct resolved_reference
    {
        uri target;                     ///< the absolute target URI, with fragment
        const json* value = nullptr;
        const node* target_node = nullptr;
    };
    [[nodiscard]] resolved_reference reference(std::string_view text) const;

    /// Compiles an ECMA-262 pattern; fails if it is invalid.
    [[nodiscard]] std::shared_ptr<const compiled_regex> regex(std::string_view pattern) const;

    /// True if the vocabulary (a `vocabulary` bit) applies to the schema being compiled.
    [[nodiscard]] bool vocabulary_enabled(unsigned vocabulary) const;

    /// Registers a `$dynamicRef` binding to be filled in once the graph is built.
    /// @p binding must stay at a stable address (it lives inside a heap-allocated keyword).
    void bind_dynamic_anchor(dynamic_anchor_binding& binding) const;

    [[nodiscard]] registry& schemas() const noexcept;

    /// The absolute keyword location: resource URI + "#" + pointer to this keyword.
    [[nodiscard]] std::string location() const;

    /// Throws `schema_error` with @p message and the keyword location.
    [[noreturn]] void fail(std::string_view message) const;

private:
    compiler& owner_;
    const node& node_;
    std::string_view keyword_;
    const json& value_;
};

/// The dialect of a schema resource (doc 1, doc 3 `$vocabulary`).
struct dialect
{
    enum class kind
    {
        standard,   ///< the 2020-12 meta-schema, or no `$schema`
        custom,     ///< a meta-schema whose own dialect is (eventually) 2020-12
        foreign     ///< another draft, accepted only with `accept_other_dialects`
    } shape = kind::standard;
    const json* meta_schema = nullptr;  ///< for `custom`
    unsigned vocabularies = 0;          ///< `vocabulary` bits that apply
    /// Vocabularies of custom keywords (`keyword_extension::vocabulary`) the meta-schema lists.
    std::vector<std::string> extension_vocabularies;
};

class compiler
{
public:
    compiler(compiled_schema& target, const compile_options& options);

    /// The dialect of @p resource: its `$schema`, else its enclosing resource's, else 2020-12.
    /// Throws `schema_error` for unsupported dialects and unsupported required vocabularies.
    const dialect& dialect_of(const schema_resource& resource);

    /// Validates the document whose root resource is @p document against its meta-schema
    /// (doc 3.1). Throws `schema_error` with the meta-schema errors if it is invalid.
    void validate_document(const schema_resource& document);

    /// Validates the document containing @p n the first time one of its nodes is compiled.
    void ensure_validated(const node& n);

    /// The node for a schema value, creating and queueing it if needed. Returns nullptr
    /// if @p schema_value is not an object or a boolean.
    [[nodiscard]] const node* node_for(const json& schema_value);

    /// Compiles every queued node, binds dynamic anchors, and rejects reference cycles.
    void run();

    [[nodiscard]] const compile_options& options() const noexcept { return options_; }
    [[nodiscard]] registry& schemas() noexcept { return target_.schemas; }
    void bind_dynamic_anchor(dynamic_anchor_binding& binding) { bindings_.push_back(&binding); }

private:
    void compile_node(node& target);
    [[nodiscard]] const keyword_extension* find_extension(std::string_view name) const noexcept;
    void drain();   ///< compiles queued nodes and binds dynamic anchors; may run nested
    dialect resolve_dialect(const schema_resource& resource);
    void check_cycles() const;
    void name_node(node& target, const schema_location& location);
    void compute_schema_info();

    compiled_schema& target_;
    const compile_options& options_;
    std::unordered_map<const json*, node*> nodes_by_schema_;
    std::vector<node*> queue_;
    std::vector<dynamic_anchor_binding*> bindings_;
    std::unordered_map<const schema_resource*, dialect> dialects_;
    std::unordered_set<const json*> validated_documents_;
};

/// Builds a compiled schema (`schema::compile`).
[[nodiscard]] std::unique_ptr<compiled_schema> compile_schema(json root, compile_options options);

/// "uri#pointer" for a node, for messages.
[[nodiscard]] std::string describe(const node& n);

} // namespace jsonschema::detail
