// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include "compiler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <unordered_set>

#include "json/pointer.hpp"
#include "keywords/keywords.hpp"
#include "metaschemas/metaschemas.hpp"

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

/// Whether a keyword that has no compiled behavior is reported as an annotation: the
/// annotation keywords of the meta-data, format and content vocabularies, plus unknown
/// keywords (2020-12 core §7.1). Identifiers, `$defs`, `$comment` and keywords compiled by a
/// sibling (`then`, `minContains`, ...) are not annotations.
bool produces_annotation(std::string_view name, const compile_options& /*options*/)
{
    static constexpr std::array<std::string_view, 12> not_annotations = {
        "$anchor", "$comment", "$defs", "$dynamicAnchor", "$id", "$schema",
        "$vocabulary", "definitions", "else", "maxContains", "minContains", "then"};
    return std::find(not_annotations.begin(), not_annotations.end(), name) == not_annotations.end();
}

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

bool compile_scope::vocabulary_enabled(unsigned vocabulary) const
{
    return (owner_.dialect_of(*node_.resource).vocabularies & vocabulary) != 0;
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
    created.location = location.resource->base + "#" + encode_fragment(location.pointer);
    if (options_.classification.enabled) {
        name_node(created, location);
    }
    nodes_by_schema_.emplace(&schema_value, &created);
    queue_.push_back(&created);
    return &created;
}

void compiler::drain()
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
}

void compiler::run()
{
    drain();
    target_.uses_dynamic_scope = !bindings_.empty();
    target_.classification_enabled = options_.classification.enabled;
    target_.complete_walk = options_.classification.complete_walk;
    target_.recover = options_.classification.recovery.recovers();
    check_cycles();
    if (options_.classification.enabled) {
        compute_schema_info();
    }
}

void compiler::name_node(node& target, const schema_location& location)
{
    // The keyword holding the node, and its member name or index (doc 4.4), from the
    // node's path in its document.
    const auto tokens = json_pointer::parse(location.document_pointer);
    std::string parent_keyword;
    std::string key;
    if (tokens) {
        const auto is_in = [](const auto& list, const std::string& token) {
            return std::find(list.begin(), list.end(), token) != list.end();
        };
        for (std::size_t i = 0; i < tokens->size();) {
            const std::string& token = (*tokens)[i];
            if ((is_in(map_subschema_keywords, token) || is_in(array_subschema_keywords, token)) && i + 1 < tokens->size()) {
                parent_keyword = token;
                key = (*tokens)[i + 1];
                i += 2;
            } else {
                parent_keyword = token;
                key.clear();
                i += 1;
            }
        }
    }
    const schema_node_view view{target.location, *target.schema, parent_keyword, key};
    if (auto name = options_.classification.name_rule(view)) {
        target.name = std::move(*name);
        target.info = std::make_unique<schema_info>();
    }
}

void compiler::compute_schema_info()
{
    // A named node declares the properties of every schema it always applies at its own
    // location: itself, and what it reaches through `$ref`, `$dynamicRef` and `allOf`.
    for (node& named : target_.nodes) {
        if (named.name.empty()) {
            continue;
        }
        std::vector<std::string> declared;
        std::vector<const node*> pending{&named};
        std::unordered_set<const node*> seen{&named};
        while (!pending.empty()) {
            const node* current = pending.back();
            pending.pop_back();
            node_facts facts;
            for (const auto& k : current->keywords) {
                k->collect_facts(facts);
            }
            for (const auto name : facts.declared_properties) {
                declared.emplace_back(name);
            }
            for (const node* next : facts.always_applied) {
                if (seen.insert(next).second) {
                    pending.push_back(next);
                }
            }
        }
        std::sort(declared.begin(), declared.end());
        declared.erase(std::unique(declared.begin(), declared.end()), declared.end());
        named.info->set_declared_properties(std::move(declared));
    }
}

void compiler::compile_node(node& target)
{
    ensure_validated(target);   // before this file's own checks, for clearer errors
    const json& schema_value = *target.schema;
    if (schema_value.is_boolean()) {
        target.always_false = !schema_value.get<bool>();
        return;
    }
    const unsigned vocabularies = dialect_of(*target.resource).vocabularies;

    struct compiled_keyword
    {
        keyword_phase phase;
        std::unique_ptr<keyword> compiled;
    };
    std::vector<compiled_keyword> compiled;
    for (auto it = schema_value.begin(); it != schema_value.end(); ++it) {
        const std::string& name = it.key();
        const json& value = it.value();
        const keyword_definition* definition = find_keyword(name);
        if (definition != nullptr && (definition->vocabulary & vocabularies) == 0) {
            definition = nullptr;   // its vocabulary is not in effect: an unknown keyword
        }
        if (definition == nullptr) {
            // Annotation keywords and unknown keywords (which 2020-12 collects as annotations)
            // are reported to an annotation sink. Identifiers, `$defs` and keywords that a
            // sibling compiles are not annotations.
            if (produces_annotation(name, options_)) {
                target.annotations.emplace_back(name, &value);
            }
            continue;
        }
        const compile_scope scope(*this, target, definition->name, value);
        if (auto result = definition->factory(scope)) {
            result->bind(definition->name, target.location + "/" + encode_fragment(json_pointer::escape(definition->name)));
            if (definition->phase == keyword_phase::unevaluated) {
                target.has_unevaluated = true;
            }
            compiled.push_back({definition->phase, std::move(result)});
        }
    }
    std::stable_sort(compiled.begin(), compiled.end(), [](const auto& a, const auto& b) { return a.phase < b.phase; });
    target.keywords.reserve(compiled.size());
    for (auto& entry : compiled) {
        target.keywords.push_back(std::move(entry.compiled));
    }
}

const dialect& compiler::dialect_of(const schema_resource& resource)
{
    if (const auto it = dialects_.find(&resource); it != dialects_.end()) {
        return it->second;
    }
    dialect d = resolve_dialect(resource);
    return dialects_.emplace(&resource, d).first->second;
}

dialect compiler::resolve_dialect(const schema_resource& resource)
{
    const json& root = *resource.root;
    const auto declared = root.is_object() ? root.find("$schema") : root.end();
    if (!root.is_object() || declared == root.end()) {
        // No `$schema`: the enclosing resource's dialect, else 2020-12 (doc 6.1).
        if (resource.parent != nullptr) {
            return dialect_of(*resource.parent);
        }
        return dialect{dialect::kind::standard, nullptr, vocab_all_2020_12};
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

    // Follow custom meta-schemas until one declares the 2020-12 dialect. The vocabularies
    // come from the meta-schema that `$schema` names directly (2020-12 core §8.1.2).
    std::optional<dialect> result;
    std::unordered_set<std::string> visited;
    for (std::size_t depth = 0; depth < max_meta_schema_chain; ++depth) {
        const std::string key = resource_key(*current);
        if (key == dialect_2020_12) {
            return result ? *result : dialect{dialect::kind::standard, nullptr, vocab_all_2020_12};
        }
        const bool known_other = std::find(other_dialects.begin(), other_dialects.end(), key) != other_dialects.end();
        if (known_other || !visited.insert(key).second) {
            if (options_.accept_other_dialects) {
                return result ? *result : dialect{dialect::kind::foreign, nullptr, vocab_all_2020_12};
            }
            if (known_other) {
                fail("unsupported dialect " + key + "; json-schema-cpp implements JSON Schema 2020-12 only");
            }
            break;
        }
        const json* meta_schema = nullptr;
        try {
            meta_schema = &target_.schemas.resolve(*current);
        } catch (const schema_error& e) {
            if (options_.accept_other_dialects) {
                return dialect{dialect::kind::foreign, nullptr, vocab_all_2020_12};
            }
            fail(std::string("unknown meta-schema: ") + e.what());
        }
        if (!result) {
            result = dialect{dialect::kind::custom, meta_schema, vocab_all_2020_12};
            if (meta_schema->is_object()) {
                if (const auto vocabularies = meta_schema->find("$vocabulary"); vocabularies != meta_schema->end()) {
                    if (!vocabularies->is_object()) {
                        fail("the meta-schema's $vocabulary must be an object");
                    }
                    result->vocabularies = vocab_core;  // always in effect
                    for (const auto& [vocabulary_uri, required] : vocabularies->items()) {
                        const unsigned bit = vocabulary_bit(vocabulary_uri);
                        if (bit != 0) {
                            result->vocabularies |= bit;
                        } else if (required.is_boolean() && required.get<bool>()) {
                            fail("the meta-schema " + key + " requires the vocabulary " + vocabulary_uri +
                                 ", which json-schema-cpp does not support");
                        }
                    }
                }
            }
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
    throw schema_error("json-schema-cpp: cannot determine the dialect of meta-schema " + resource_key(*current) + " (at " +
                       resource.base + "#/$schema)");
}

namespace {

/// The 2020-12 meta-schema, compiled once per process. Immutable, so shared by all threads.
const compiled_schema& standard_metaschema()
{
    static const std::unique_ptr<compiled_schema> compiled = [] {
        compile_options options;
        options.validate_against_metaschema = false;
        return compile_schema(json::parse(*builtin_document(dialect_2020_12)), std::move(options));
    }();
    return *compiled;
}

/// Errors from validating @p document against @p meta.
std::vector<error> metaschema_errors(const node& meta, bool track_dynamic_scope, const json& document)
{
    collecting_error_sink sink;
    eval_context::settings settings;
    settings.track_dynamic_scope = track_dynamic_scope;
    settings.errors = &sink;
    eval_context context(settings);
    static_cast<void>(evaluate(meta, document, context, nullptr, edge::root));
    context.flush_errors();
    return std::move(sink.errors);
}

} // namespace

void compiler::validate_document(const schema_resource& document)
{
    if (document.builtin) {
        return;
    }
    const dialect& d = dialect_of(document);
    std::vector<error> errors;
    std::string meta_uri;
    if (d.shape == dialect::kind::standard) {
        const compiled_schema& meta = standard_metaschema();
        errors = metaschema_errors(*meta.root, meta.uses_dynamic_scope, *document.root);
        meta_uri = std::string(dialect_2020_12);
    } else if (d.shape == dialect::kind::custom) {
        const node* meta = node_for(*d.meta_schema);
        if (meta == nullptr) {
            throw schema_error("json-schema-cpp: the meta-schema of " + document.base + " is not a schema");
        }
        drain();
        errors = metaschema_errors(*meta, !bindings_.empty(), *document.root);
        meta_uri = meta->resource->base;
    } else {
        return;     // another draft: there is no 2020-12 meta-schema to check it against
    }
    if (errors.empty()) {
        return;
    }
    std::string message = "json-schema-cpp: the schema " + document.base + " is not valid against its meta-schema " + meta_uri + ": ";
    // The vocabulary meta-schemas often report the same problem once each; list it once.
    std::vector<std::string> distinct;
    for (const auto& e : errors) {
        std::string line = std::string("at ") + (e.instance_location.empty() ? "the root" : e.instance_location.to_string()) + ": " + e.message;
        if (std::find(distinct.begin(), distinct.end(), line) == distinct.end()) {
            distinct.push_back(std::move(line));
        }
    }
    constexpr std::size_t shown = 3;
    for (std::size_t i = 0; i < distinct.size() && i < shown; ++i) {
        message += (i == 0 ? "" : "; ") + distinct[i];
    }
    if (distinct.size() > shown) {
        message += "; and " + std::to_string(distinct.size() - shown) + " more";
    }
    for (auto& e : errors) {
        e.instance = nullptr;   // the schema document does not outlive the exception
    }
    throw schema_error(message, std::move(errors));
}

void compiler::ensure_validated(const node& n)
{
    const json* document = n.resource->document;
    if (!options_.validate_against_metaschema || !validated_documents_.insert(document).second) {
        return;
    }
    if (const schema_resource* root = target_.schemas.resource_rooted_at(*document)) {
        validate_document(*root);
    }
}

void compiler::check_cycles() const
{
    // Depth-first search over in-place edges ($ref, allOf, ...). A cycle means the same
    // schema would be applied to the same instance location forever.
    enum class state : std::uint8_t { unvisited, active, done };
    std::unordered_map<const node*, state> states;
    struct dfs_frame
    {
        const node* current;
        std::vector<const node*> children;
        std::size_t next = 0;
    };
    for (const node& start : target_.nodes) {
        if (states[&start] != state::unvisited) {
            continue;
        }
        std::vector<dfs_frame> stack;
        const auto push = [&](const node* n) {
            states[n] = state::active;
            dfs_frame f{n, {}, 0};
            for (const auto& k : n->keywords) {
                k->in_place_subschemas(f.children);
            }
            stack.push_back(std::move(f));
        };
        push(&start);
        while (!stack.empty()) {
            dfs_frame& top = stack.back();
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

    if (!root_value.is_object() && !root_value.is_boolean()) {
        throw schema_error("json-schema-cpp: a schema must be an object or a boolean");
    }
    compiler builder(*result, options);
    const schema_resource* root_resource = schemas.location_of(root_value).resource;
    result->root = builder.node_for(root_value);
    builder.run();      // each document is validated against its meta-schema as it is first compiled
    result->base_uri = root_resource->base_uri;
    return result;
}

} // namespace jsonschema::detail
