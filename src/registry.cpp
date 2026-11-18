// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include "registry.hpp"

#include <exception>
#include <utility>

#include "json/pointer.hpp"

namespace jsonschema::detail {

namespace {

/// 2020-12 core §8.2.2: an anchor is a plain name fragment, `^[A-Za-z_][-A-Za-z0-9._]*$`.
bool is_valid_anchor_name(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    const auto is_alpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (!is_alpha(name.front()) && name.front() != '_') {
        return false;
    }
    for (const char c : name.substr(1)) {
        if (!is_alpha(c) && !(c >= '0' && c <= '9') && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

std::string describe(const schema_resource& resource, const std::string& pointer)
{
    return resource.base + "#" + pointer;
}

} // namespace

std::string resource_key(const uri& absolute)
{
    return absolute.without_fragment().normalized().to_string();
}

registry::registry(schema_loader loader) : loader_(std::move(loader)) {}

schema_resource* registry::find(const std::string& key) const
{
    const auto it = by_uri_.find(key);
    return it == by_uri_.end() ? nullptr : it->second;
}

bool registry::knows(const uri& absolute) const
{
    return find(resource_key(absolute)) != nullptr;
}

schema_resource* registry::add_resource(const uri& base, const json& root)
{
    const uri normalized = base.without_fragment().normalized();
    std::string key = normalized.to_string();
    if (schema_resource* existing = find(key)) {
        if (existing->root == &root) {
            return existing;
        }
        throw schema_error("json-schema-cpp: two schema resources share the URI " + key);
    }
    schema_resource& resource = resources_.emplace_back();
    resource.base_uri = normalized;
    resource.base = std::move(key);
    resource.root = &root;
    by_uri_.emplace(resource.base, &resource);
    resource_roots_.emplace(&root, &resource);
    return &resource;
}

const json& registry::add_document(json document, const uri& retrieval_uri)
{
    if (!retrieval_uri.has_scheme()) {
        throw schema_error("json-schema-cpp: a schema document's URI must be absolute: " + retrieval_uri.to_string());
    }
    const json& root = *documents_.emplace_back(std::make_unique<json>(std::move(document)));

    uri base = retrieval_uri.without_fragment();
    if (root.is_object()) {
        if (const auto id = root.find("$id"); id != root.end()) {
            if (!id->is_string()) {
                throw schema_error("json-schema-cpp: $id must be a string at " + base.to_string() + "#");
            }
            const auto reference = uri::parse(id->get_ref<const std::string&>());
            if (!reference || (reference->fragment() && !reference->fragment()->empty())) {
                throw schema_error("json-schema-cpp: invalid $id \"" + id->get<std::string>() + "\" at " + base.to_string() + "#");
            }
            base = base.resolve(*reference);
        }
    }
    schema_resource* resource = add_resource(base, root);
    // The retrieval URI also identifies the document root.
    const std::string retrieval_key = resource_key(retrieval_uri);
    if (retrieval_key != resource->base) {
        if (schema_resource* existing = find(retrieval_key); existing && existing != resource) {
            throw schema_error("json-schema-cpp: two schema resources share the URI " + retrieval_key);
        }
        by_uri_.emplace(retrieval_key, resource);
    }
    discover(root, resource, std::string(), true);
    return root;
}

void registry::discover(const json& value, schema_resource* resource, const std::string& pointer, bool is_resource_root)
{
    if (!value.is_object()) {
        if (value.is_boolean()) {
            locations_.try_emplace(&value, schema_location{resource, pointer});
        }
        return;
    }

    std::string here = pointer;
    if (!is_resource_root) {
        if (const auto id = value.find("$id"); id != value.end()) {
            if (!id->is_string()) {
                throw schema_error("json-schema-cpp: $id must be a string at " + describe(*resource, pointer));
            }
            const auto reference = uri::parse(id->get_ref<const std::string&>());
            if (!reference || (reference->fragment() && !reference->fragment()->empty())) {
                throw schema_error("json-schema-cpp: invalid $id \"" + id->get<std::string>() + "\" at " + describe(*resource, pointer));
            }
            resource = add_resource(resource->base_uri.resolve(*reference), value);
            here.clear();
        }
    }
    if (!locations_.try_emplace(&value, schema_location{resource, here}).second) {
        return; // already discovered
    }

    const auto add_anchor = [&](std::string_view keyword, bool dynamic) {
        const auto it = value.find(keyword);
        if (it == value.end()) {
            return;
        }
        if (!it->is_string() || !is_valid_anchor_name(it->get_ref<const std::string&>())) {
            throw schema_error("json-schema-cpp: invalid " + std::string(keyword) + " at " + describe(*resource, here));
        }
        const std::string& name = it->get_ref<const std::string&>();
        const auto [existing, inserted] = resource->anchors.try_emplace(name, &value);
        if (!inserted && existing->second != &value) {
            throw schema_error("json-schema-cpp: duplicate anchor \"" + name + "\" in " + resource->base);
        }
        if (dynamic) {
            resource->dynamic_anchors.try_emplace(name, &value);
        }
    };
    add_anchor("$anchor", false);
    add_anchor("$dynamicAnchor", true);

    for (const std::string_view keyword : single_subschema_keywords) {
        if (const auto it = value.find(keyword); it != value.end()) {
            std::string child = here;
            json_pointer::append(child, keyword);
            discover(*it, resource, child, false);
        }
    }
    for (const std::string_view keyword : array_subschema_keywords) {
        if (const auto it = value.find(keyword); it != value.end() && it->is_array()) {
            for (std::size_t i = 0; i < it->size(); ++i) {
                std::string child = here;
                json_pointer::append(child, keyword);
                json_pointer::append(child, std::to_string(i));
                discover((*it)[i], resource, child, false);
            }
        }
    }
    for (const std::string_view keyword : map_subschema_keywords) {
        if (const auto it = value.find(keyword); it != value.end() && it->is_object()) {
            for (const auto& [name, subschema] : it->items()) {
                std::string child = here;
                json_pointer::append(child, keyword);
                json_pointer::append(child, name);
                discover(subschema, resource, child, false);
            }
        }
    }
}

const json& registry::resolve(const uri& target)
{
    if (!target.has_scheme()) {
        throw schema_error("json-schema-cpp: cannot resolve relative URI " + target.to_string());
    }
    const uri document_uri = target.without_fragment().normalized();
    const std::string key = document_uri.to_string();
    schema_resource* resource = find(key);
    if (resource == nullptr) {
        std::optional<json> document;
        if (loader_) {
            try {
                document = loader_(document_uri);
            } catch (const schema_error&) {
                throw;
            } catch (const std::exception& e) {
                throw schema_error("json-schema-cpp: loading " + key + " failed: " + e.what());
            }
        }
        if (!document) {
            throw schema_error("json-schema-cpp: cannot resolve " + target.to_string() + ": no schema with URI " + key +
                               " is known" + (loader_ ? " and the loader could not load it" : " and no loader is set"));
        }
        add_document(std::move(*document), document_uri);
        resource = find(key);
    }

    if (!target.fragment() || target.fragment()->empty()) {
        return *resource->root;
    }
    const auto fragment = percent_decode(*target.fragment());
    if (!fragment) {
        throw schema_error("json-schema-cpp: invalid percent-encoding in " + target.to_string());
    }
    if (fragment->front() == '/') {
        const json* value = json_pointer::navigate(*resource->root, *fragment);
        if (value == nullptr) {
            throw schema_error("json-schema-cpp: cannot resolve " + target.to_string() + ": the JSON pointer matches nothing");
        }
        if (!locations_.contains(value)) {
            // A location inside an unknown keyword or other non-subschema position (doc 2.3).
            discover(*value, resource, *fragment, false);
        }
        return *value;
    }
    const auto anchor = resource->anchors.find(*fragment);
    if (anchor == resource->anchors.end()) {
        throw schema_error("json-schema-cpp: cannot resolve " + target.to_string() + ": no anchor \"" + *fragment + "\" in " + key);
    }
    return *anchor->second;
}

const schema_location& registry::location_of(const json& value) const
{
    const auto it = locations_.find(&value);
    if (it == locations_.end()) {
        throw schema_error("json-schema-cpp: internal error: schema value has no known location");
    }
    return it->second;
}

const schema_resource* registry::resource_rooted_at(const json& value) const
{
    const auto it = resource_roots_.find(&value);
    return it == resource_roots_.end() ? nullptr : it->second;
}

} // namespace jsonschema::detail
