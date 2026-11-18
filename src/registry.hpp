// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The schema registry (design doc 2.3): owns every schema document and maps absolute
// URIs, anchors and JSON-pointer fragments to schema values.
#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <jsonschema/schema.hpp>
#include <jsonschema/uri.hpp>

namespace jsonschema::detail {

/// A schema resource: a schema with an absolute base URI (a document root or a subschema
/// with `$id`), plus the anchors defined in it.
struct schema_resource
{
    uri base_uri;                    ///< absolute, normalized, no fragment
    std::string base;                ///< base_uri.to_string()
    const json* root = nullptr;
    std::string document_pointer;    ///< JSON pointer from the document root to `root`
    const json* document = nullptr;  ///< the document's root value
    const schema_resource* parent = nullptr;   ///< enclosing resource, for embedded resources
    bool builtin = false;            ///< one of the embedded 2020-12 meta-schemas
    std::unordered_map<std::string, const json*> anchors;            ///< `$anchor` and `$dynamicAnchor`
    std::unordered_map<std::string, const json*> dynamic_anchors;    ///< `$dynamicAnchor` only
};

/// Where a schema value lives: its innermost enclosing resource and the JSON pointer
/// from that resource's root.
struct schema_location
{
    const schema_resource* resource = nullptr;
    std::string pointer;            ///< from the resource root
    std::string document_pointer;   ///< from the document root (differs inside embedded resources)
};

/// 2020-12 keywords whose values are subschemas, by shape. Discovery (and so `$id` and
/// anchor recognition) follows exactly these. Two superseded keywords that the 2020-12
/// meta-schema still describes are included: `definitions`, which implementations SHOULD
/// treat like `$defs` (validation spec, Appendix A), and `dependencies`, whose object-valued
/// members are schemas (array-valued members are skipped, as they are not schemas).
inline constexpr std::array<std::string_view, 11> single_subschema_keywords = {
    "additionalProperties", "contains", "contentSchema", "else", "if", "items",
    "not", "propertyNames", "then", "unevaluatedItems", "unevaluatedProperties"};
inline constexpr std::array<std::string_view, 4> array_subschema_keywords = {"allOf", "anyOf", "oneOf", "prefixItems"};
inline constexpr std::array<std::string_view, 6> map_subschema_keywords = {
    "$defs", "definitions", "dependencies", "dependentSchemas", "patternProperties", "properties"};

/// The 2020-12 meta-schema URI.
inline constexpr std::string_view dialect_2020_12 = "https://json-schema.org/draft/2020-12/schema";

class registry
{
public:
    explicit registry(schema_loader loader);

    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;

    /// Takes ownership of @p document and registers it under @p retrieval_uri (absolute,
    /// no fragment) and under its root `$id`, if any. Returns the stored root.
    const json& add_document(json document, const uri& retrieval_uri);

    /// True if a resource with this absolute URI (fragment ignored) is registered.
    [[nodiscard]] bool knows(const uri& absolute) const;

    /// Resolves an absolute URI, with an optional fragment, to a value. Unknown documents
    /// are fetched with the loader. Throws `schema_error` if the URI cannot be resolved.
    const json& resolve(const uri& target);

    /// Discovers @p value, a subschema at @p relative_pointer (escaped, starting with "/")
    /// below the schema object @p container, which must have a known location. For
    /// subschemas of custom keywords, which discovery does not follow.
    void discover_below(const json& container, const json& value, std::string_view relative_pointer);

    /// The location of a schema value that was discovered or resolved. Throws
    /// `schema_error` for values the registry has never seen.
    [[nodiscard]] const schema_location& location_of(const json& value) const;

    /// Root resources of every document, in registration order.
    [[nodiscard]] const std::vector<const schema_resource*>& document_resources() const noexcept { return document_resources_; }

    /// The resource whose root is @p value, or nullptr.
    [[nodiscard]] const schema_resource* resource_rooted_at(const json& value) const;

    /// Resources in registration order. Indexes are stable; the deque only grows.
    [[nodiscard]] const std::deque<schema_resource>& resources() const noexcept { return resources_; }

private:
    schema_resource* add_resource(const uri& base, const json& root, const std::string& document_pointer,
                                  const schema_resource* parent);
    void discover(const json& value, schema_resource* resource, const std::string& pointer,
                  const std::string& document_pointer, bool is_resource_root);
    schema_resource* find(const std::string& key) const;

    schema_loader loader_;
    std::deque<std::unique_ptr<json>> documents_;
    std::deque<schema_resource> resources_;
    std::unordered_map<std::string, schema_resource*> by_uri_;
    std::unordered_map<const json*, schema_location> locations_;
    std::unordered_map<const json*, schema_resource*> resource_roots_;
    std::vector<const schema_resource*> document_resources_;
    const json* current_document_ = nullptr;
    bool adding_builtin_ = false;
};

/// The canonical text of a resource URI: normalized, without fragment.
[[nodiscard]] std::string resource_key(const uri& absolute);

} // namespace jsonschema::detail
