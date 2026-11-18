// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Schema classification (design doc 4): which named schemas each instance location matched.
#pragma once

#include <functional>
#include <optional>
#include <span>
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

/// What a naming rule sees for each compiled schema node.
struct schema_node_view
{
    std::string_view schema_location;   ///< absolute URI of the node
    const json& schema;                 ///< the node's schema value
    std::string_view parent_keyword;    ///< the keyword whose value holds the node ("$defs", "properties", "items", ...); empty for a document root
    std::string_view key;               ///< the member name or array index within that keyword; empty for single-schema keywords
};

/// Assigns classification names to schema nodes at compile time (doc 4.4).
class JSONSCHEMA_EXPORT namer
{
public:
    using rule = std::function<std::optional<std::string>(const schema_node_view&)>;

    /// Names every direct child of `$defs` (or the superseded `definitions`) with its key. The default.
    [[nodiscard]] static namer defs_children();
    /// Names every schema with an `$anchor` by the anchor.
    [[nodiscard]] static namer anchors();
    [[nodiscard]] static namer custom(rule r);

    /// The first rule that produces a name wins.
    [[nodiscard]] namer operator|(const namer& other) const;

    [[nodiscard]] std::optional<std::string> operator()(const schema_node_view& view) const;

private:
    std::vector<rule> rules_;
};

/// How classification treats schemas that fail (doc 4.6).
class JSONSCHEMA_EXPORT recovery_policy
{
public:
    /// Strict annotation rules: a schema that fails classifies nothing.
    [[nodiscard]] static recovery_policy none() { return recovery_policy(false); }
    /// A failure counts as "not this kind of thing" only for `type`, `const` or `enum` at the
    /// node itself, or a failing discriminator property (one whose subschema is a `const` or a
    /// single-value `enum`, missing when required or present with the wrong value). Any other
    /// failure keeps the classification, marked `recovered`. The default.
    [[nodiscard]] static recovery_policy discriminators() { return recovery_policy(true); }

    [[nodiscard]] bool recovers() const noexcept { return recovers_; }

private:
    explicit recovery_policy(bool recovers) : recovers_(recovers) {}
    bool recovers_;
};

struct classification_options
{
    bool enabled = false;                                   ///< compile-time switch; off costs nothing
    namer name_rule = namer::defs_children();
    recovery_policy recovery = recovery_policy::discriminators();
    bool complete_walk = true;                              ///< every object and array gets exactly one callback
};

/// Compile-time facts about a named schema node, for consumers (doc 4.5).
class JSONSCHEMA_EXPORT schema_info
{
public:
    /// True if @p name is listed in the node's `properties`, or in the `properties` of a schema
    /// it always applies at the same location (through `$ref`, `$dynamicRef` or `allOf`).
    [[nodiscard]] bool declares_property(std::string_view name) const;

    /// Internal: sets the declared property names (sorted, unique).
    void set_declared_properties(std::vector<std::string> names) { declared_ = std::move(names); }

private:
    std::vector<std::string> declared_;
};

/// How evaluation entered a named node, relative to the previous match at the same location.
enum class via
{
    root,       ///< the root schema itself
    child,      ///< from the parent instance through `properties`, `items`, `additionalProperties`, ...
    ref,        ///< through `$ref` or `$dynamicRef` at the same instance location
    in_place    ///< through `allOf`, `anyOf`, `oneOf`, `if`/`then`/`else` or `dependentSchemas`
};

enum class confidence
{
    exact,      ///< the named schema passed and was chosen by passing
    recovered,  ///< the named schema failed with content errors only, or was the only plausible failing branch
    ambiguous   ///< one of several equally plausible candidates
};

/// The weakest match decides.
enum class classification_status
{
    exact,
    recovered,
    ambiguous,
    unclassified
};

struct match
{
    std::string_view name;              ///< e.g. "event"
    std::string_view schema_location;   ///< absolute URI of the named node
    via reached_by;
    jsonschema::confidence confidence;
    const schema_info& info;
};

struct classification
{
    json::json_pointer instance_location;
    const json& instance;                   ///< valid during the callback
    std::span<const match> matches;         ///< outermost first; empty only in complete-walk mode
    classification_status status;
};

/// Receives one callback per classified instance location, in document order, after
/// validation finishes. With `complete_walk`, every object and array gets one.
class JSONSCHEMA_EXPORT classification_sink
{
public:
    virtual ~classification_sink() = default;
    virtual void on_classified(const classification& c) = 0;
};

} // namespace jsonschema

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
