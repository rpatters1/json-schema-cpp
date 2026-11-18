// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Standard output formats (2020-12 §12.4): flag, basic, detailed.
#include <jsonschema/schema.hpp>

#include <memory>
#include <string>
#include <vector>

#include "compiler.hpp"
#include "node.hpp"

namespace jsonschema {

namespace {

class null_error_sink final : public error_sink
{
public:
    void on_error(const error& /*e*/) override {}
};

/// One leaf: an error or an annotation, with the subschema evaluations that enclose it.
struct leaf
{
    json unit;
    const std::vector<detail::output_unit>* chain;
};

/// A node of the "detailed" tree: one subschema evaluation and what happened under it.
struct tree_node
{
    std::string keyword_location;
    std::string absolute_location;
    std::string instance_location;
    std::vector<std::unique_ptr<tree_node>> children;   // in first-seen order
    std::vector<json> leaves;
    std::vector<int> order;     // 0: next child, 1: next leaf, interleaved in insertion order

    tree_node* child(const detail::output_unit& u)
    {
        for (auto& c : children) {
            if (c->keyword_location == u.keyword_location && c->instance_location == u.instance_location) {
                return c.get();
            }
        }
        auto created = std::make_unique<tree_node>();
        created->keyword_location = u.keyword_location;
        created->absolute_location = std::string(u.absolute_location);
        created->instance_location = u.instance_location;
        children.push_back(std::move(created));
        order.push_back(0);
        return children.back().get();
    }
};

json unit_header(bool valid, const std::string& keyword_location, const std::string& absolute, const std::string& instance)
{
    json unit = json::object();
    unit["valid"] = valid;
    unit["keywordLocation"] = keyword_location;
    if (!absolute.empty()) {
        unit["absoluteKeywordLocation"] = absolute;
    }
    unit["instanceLocation"] = instance;
    return unit;
}

/// Serializes @p node's contents, collapsing any child that has exactly one entry.
json contents(const tree_node& node, bool valid, const char* list_key)
{
    json list = json::array();
    std::size_t next_child = 0;
    std::size_t next_leaf = 0;
    for (const int kind : node.order) {
        if (kind == 1) {
            list.push_back(node.leaves[next_leaf++]);
            continue;
        }
        const tree_node& c = *node.children[next_child++];
        json inner = contents(c, valid, list_key);
        if (inner.size() == 1) {
            list.push_back(std::move(inner[0]));    // a level with one entry adds nothing
        } else {
            json unit = unit_header(valid, c.keyword_location, c.absolute_location, c.instance_location);
            unit[list_key] = std::move(inner);
            list.push_back(std::move(unit));
        }
    }
    return list;
}

json detailed(const std::vector<leaf>& leaves, bool valid, const char* list_key)
{
    tree_node root;
    for (const auto& l : leaves) {
        tree_node* node = &root;
        // chain[0] is the root schema itself, which is the top-level unit.
        for (std::size_t i = 1; i < l.chain->size(); ++i) {
            node = node->child((*l.chain)[i]);
        }
        node->leaves.push_back(l.unit);
        node->order.push_back(1);
    }
    json out = unit_header(valid, "", "", "");
    json inner = contents(root, valid, list_key);
    if (!inner.empty()) {
        out[list_key] = std::move(inner);
    }
    return out;
}

} // namespace

json schema::output(const json& instance, output_format format) const
{
    if (format == output_format::flag) {
        return json{{"valid", is_valid(instance)}};
    }

    null_error_sink sink;
    std::vector<detail::eval_context::error_entry> errors;
    detail::eval_context::settings settings;
    settings.track_dynamic_scope = impl_->uses_dynamic_scope;
    settings.errors = &sink;
    settings.annotations = true;
    settings.record_chains = format == output_format::detailed;
    settings.captured_errors = &errors;
    detail::eval_context context(settings);
    const bool valid = detail::evaluate(*impl_->root, instance, context, nullptr, detail::edge::root);
    context.flush_errors();

    std::vector<leaf> leaves;
    if (!valid) {
        for (const auto& e : errors) {
            json unit = unit_header(false, e.evaluation_path, std::string(e.schema_location), e.instance_location.to_string());
            unit["error"] = e.message;
            leaves.push_back({std::move(unit), &e.chain});
        }
    } else {
        for (const auto& a : context.annotations_) {
            json unit = unit_header(true, a.evaluation_path, a.schema_location, a.instance_location.to_string());
            unit["annotation"] = *a.value;
            leaves.push_back({std::move(unit), &a.chain});
        }
    }
    const char* list_key = valid ? "annotations" : "errors";

    if (format == output_format::detailed) {
        return detailed(leaves, valid, list_key);
    }
    json out = json::object();
    out["valid"] = valid;
    if (!leaves.empty()) {
        json list = json::array();
        for (auto& l : leaves) {
            list.push_back(std::move(l.unit));
        }
        out[list_key] = std::move(list);
    }
    return out;
}

} // namespace jsonschema
