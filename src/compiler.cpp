// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include "compiler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <unordered_set>

#include "json/pointer.hpp"
#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace {

/// Meta-schemas of drafts this library does not implement (doc 1: 2020-12 only).
constexpr std::array<std::string_view, 8> other_dialects = {
    "http://json-schema.org/schema",
    "http://json-schema.org/draft-03/schema",
    "http://json-schema.org/draft-04/schema",
    "http://json-schema.org/draft-06/schema",
    "http://json-schema.org/draft-07/schema",
    "https://json-schema.org/draft/2019-09/schema",
    "https://json-schema.org/draft/next/schema",
    "https://json-schema.org/v1",
};

constexpr std::size_t max_meta_schema_chain = 16;

/// A random (version 4) `urn:uuid:` URI, the base of a schema with no other base URI (doc 2.3).
uri generated_base_uri()
{
    std::random_device device;
    std::mt19937_64 engine((static_cast<std::uint64_t>(device()) << 32) ^ device());
    std::uint64_t high = engine();
    std::uint64_t low = engine();
    high = (high & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;   // version 4
    low = (low & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;     // RFC 4122 variant
    static constexpr char hex[] = "0123456789abcdef";
    std::string text = "urn:uuid:";
    const auto put = [&](std::uint64_t value, int first_nibble, int last_nibble) {
        for (int shift = first_nibble; shift >= last_nibble; --shift) {
            text += hex[(value >> (4 * shift)) & 0xF];
        }
    };
    put(high, 15, 8);
    text += '-';
    put(high, 7, 4);
    text += '-';
    put(high, 3, 0);
    text += '-';
    put(low, 15, 12);
    text += '-';
    put(low, 11, 0);
    return uri(text);
}

} // namespace

std::string describe(const node& n)
{
    return n.resource->base + "#" + n.pointer;
}

// ---------------------------------------------------------------------------
// compile_scope
// ---------------------------------------------------------------------------

const json* compile_scope::sibling(std::string_view name) const
{
    const auto it = node_.schema->find(name);
    return it == node_.schema->end() ? nullptr : &*it;
}

const compile_options& compile_scope::options() const noexcept
{
    return owner_.options();
}

registry& compile_scope::schemas() const noexcept
{
    return owner_.schemas();
}

const node* compile_scope::subschema(const json& value) const
{
    const node* result = owner_.node_for(value);
    if (result == nullptr) {
        fail("expected a schema (an object or a boolean)");
    }
    return result;
}

compile_scope::resolved_reference compile_scope::reference(std::string_view text) const
{
    const auto parsed = uri::parse(text);
    if (!parsed) {
        fail("invalid URI reference \"" + std::string(text) + "\"");
    }
    resolved_reference result;
    result.target = node_.resource->base_uri.resolve(*parsed);
    try {
        result.value = &owner_.schemas().resolve(result.target);
    } catch (const schema_error& e) {
        fail(e.what());
    }
    result.target_node = owner_.node_for(*result.value);
    if (result.target_node == nullptr) {
        fail("the reference " + result.target.to_string() + " does not identify a schema");
    }
    return result;
}

std::shared_ptr<const compiled_regex> compile_scope::regex(std::string_view pattern) const
{
    std::shared_ptr<const compiled_regex> result = compile_regex(pattern);
    if (!result) {
        fail("invalid regular expression \"" + std::string(pattern) + "\"");
    }
    return result;
}

void compile_scope::bind_dynamic_anchor(dynamic_anchor_binding& binding) const
{
    owner_.bind_dynamic_anchor(binding);
}

std::string compile_scope::location() const
{
    std::string pointer = node_.pointer;
    json_pointer::append(pointer, keyword_);
    return node_.resource->base + "#" + pointer;
}

void compile_scope::fail(std::string_view message) const
{
    throw schema_error("json-schema-cpp: " + std::string(message) + " (at " + location() + ")");
}

// ---------------------------------------------------------------------------
// compiler
// ---------------------------------------------------------------------------

compiler::compiler(compiled_schema& target, const compile_options& options) : target_(target), options_(options) {}

const node* compiler::node_for(const json& schema_value)
{
    if (const auto it = nodes_by_schema_.find(&schema_value); it != nodes_by_schema_.end()) {
        return it->second;
    }
    if (!schema_value.is_object() && !schema_value.is_boolean()) {
        return nullptr;
    }
    const schema_location& location = target_.schemas.location_of(schema_value);
    node& created = target_.nodes.emplace_back();
    created.schema = &schema_value;
    created.resource = location.resource;
    created.pointer = location.pointer;
    nodes_by_schema_.emplace(&schema_value, &created);
    queue_.push_back(&created);
    return &created;
}

void compiler::run()
{
    for (;;) {
        while (!queue_.empty()) {
            node* next = queue_.back();
            queue_.pop_back();
            compile_node(*next);
        }
        // Bind every `$dynamicRef` to each resource's `$dynamicAnchor` of the same name.
        // Compiling those targets can add more references, so repeat until nothing changes.
        bool added = false;
        for (std::size_t b = 0; b < bindings_.size(); ++b) {
            dynamic_anchor_binding& binding = *bindings_[b];
            const auto& resources = target_.schemas.resources();
            for (std::size_t r = 0; r < resources.size(); ++r) {
                const schema_resource& resource = resources[r];
                const auto anchor = resource.dynamic_anchors.find(binding.anchor);
                if (anchor == resource.dynamic_anchors.end()) {
                    continue;
                }
                const bool known = std::any_of(binding.candidates.begin(), binding.candidates.end(),
                                               [&](const auto& candidate) { return candidate.first == &resource; });
                if (!known) {
                    binding.candidates.emplace_back(&resource, node_for(*anchor->second));
                    added = true;
                }
            }
        }
        if (!added && queue_.empty()) {
            break;
        }
    }
    target_.uses_dynamic_scope = !bindings_.empty();
    check_cycles();
}

void compiler::compile_node(node& target)
{
    const json& schema_value = *target.schema;
    if (schema_value.is_boolean()) {
        target.always_false = !schema_value.get<bool>();
        return;
    }
    if (const schema_resource* resource = target_.schemas.resource_rooted_at(schema_value)) {
        check_dialect(schema_value, *resource);
    }

    struct compiled_keyword
    {
        keyword_phase phase;
        std::unique_ptr<keyword> compiled;
    };
    std::vector<compiled_keyword> compiled;
    for (const auto& [name, value] : schema_value.items()) {
        const keyword_definition* definition = find_keyword(name);
        if (definition == nullptr) {
            continue;   // annotations, unknown keywords, and keywords compiled by a sibling
        }
        const compile_scope scope(*this, target, definition->name, value);
        if (auto result = definition->factory(scope)) {
            compiled.push_back({definition->phase, std::move(result)});
        }
    }
    std::stable_sort(compiled.begin(), compiled.end(), [](const auto& a, const auto& b) { return a.phase < b.phase; });
    target.keywords.reserve(compiled.size());
    for (auto& entry : compiled) {
        target.keywords.push_back(std::move(entry.compiled));
    }
}

void compiler::check_dialect(const json& schema_value, const schema_resource& resource)
{
    const auto declared = schema_value.find("$schema");
    if (declared == schema_value.end()) {
        return;
    }
    const auto fail = [&](const std::string& message) {
        throw schema_error("json-schema-cpp: " + message + " (at " + resource.base + "#/$schema)");
    };
    if (!declared->is_string()) {
        fail("$schema must be a string");
    }
    auto current = uri::parse(declared->get_ref<const std::string&>());
    if (!current || !current->has_scheme()) {
        fail("$schema must be an absolute URI");
    }

    // Follow custom meta-schemas until one declares the 2020-12 dialect. Vocabularies are
    // not yet consulted (milestone 3), so any such meta-schema means "all of 2020-12".
    std::unordered_set<std::string> visited;
    for (std::size_t depth = 0; depth < max_meta_schema_chain; ++depth) {
        const std::string key = resource_key(*current);
        if (key == dialect_2020_12 || options_.accept_other_dialects) {
            return;
        }
        if (std::find(other_dialects.begin(), other_dialects.end(), key) != other_dialects.end()) {
            fail("unsupported dialect " + key + "; json-schema-cpp implements JSON Schema 2020-12 only");
        }
        if (!visited.insert(key).second) {
            break;
        }
        const json* meta_schema = nullptr;
        try {
            meta_schema = &target_.schemas.resolve(*current);
        } catch (const schema_error& e) {
            fail(std::string("unknown meta-schema: ") + e.what());
        }
        const auto next = meta_schema->is_object() ? meta_schema->find("$schema") : meta_schema->end();
        if (!meta_schema->is_object() || next == meta_schema->end() || !next->is_string()) {
            fail("meta-schema " + key + " does not declare its own $schema");
        }
        current = uri::parse(next->get_ref<const std::string&>());
        if (!current || !current->has_scheme()) {
            fail("meta-schema " + key + " has an invalid $schema");
        }
    }
    fail("cannot determine the dialect of meta-schema " + resource_key(*current));
}

void compiler::check_cycles() const
{
    // Depth-first search over in-place edges ($ref, allOf, ...). A cycle means the same
    // schema would be applied to the same instance location forever.
    enum class state : std::uint8_t { unvisited, active, done };
    std::unordered_map<const node*, state> states;
    struct frame
    {
        const node* current;
        std::vector<const node*> children;
        std::size_t next = 0;
    };
    for (const node& start : target_.nodes) {
        if (states[&start] != state::unvisited) {
            continue;
        }
        std::vector<frame> stack;
        const auto push = [&](const node* n) {
            states[n] = state::active;
            frame f{n, {}, 0};
            for (const auto& k : n->keywords) {
                k->in_place_subschemas(f.children);
            }
            stack.push_back(std::move(f));
        };
        push(&start);
        while (!stack.empty()) {
            frame& top = stack.back();
            if (top.next == top.children.size()) {
                states[top.current] = state::done;
                stack.pop_back();
                continue;
            }
            const node* child = top.children[top.next++];
            const state child_state = states[child];
            if (child_state == state::active) {
                throw schema_error("json-schema-cpp: infinite recursion: " + describe(*child) +
                                   " applies itself to the same instance location (via " + describe(*top.current) + ")");
            }
            if (child_state == state::unvisited) {
                push(child);
            }
        }
    }
}

std::unique_ptr<compiled_schema> compile_schema(json root, compile_options options)
{
    auto result = std::make_unique<compiled_schema>(std::move(options.loader));
    registry& schemas = result->schemas;

    for (auto& extra : options.resources) {
        const auto retrieval = uri::parse(extra.uri);
        if (!retrieval || !retrieval->has_scheme()) {
            throw schema_error("json-schema-cpp: resource URI must be absolute: " + extra.uri);
        }
        schemas.add_document(std::move(extra.document), *retrieval);
    }
    options.resources.clear();

    uri base;
    if (options.base_uri) {
        const auto parsed = uri::parse(*options.base_uri);
        if (!parsed || !parsed->has_scheme()) {
            throw schema_error("json-schema-cpp: base_uri must be an absolute URI: " + *options.base_uri);
        }
        base = *parsed;
    } else {
        base = generated_base_uri();
    }
    const json& root_value = schemas.add_document(std::move(root), base);

    compiler builder(*result, options);
    result->root = builder.node_for(root_value);
    if (result->root == nullptr) {
        throw schema_error("json-schema-cpp: a schema must be an object or a boolean");
    }
    builder.run();
    result->base_uri = schemas.location_of(root_value).resource->base_uri;
    return result;
}

} // namespace jsonschema::detail
