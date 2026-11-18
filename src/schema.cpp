// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <jsonschema/schema.hpp>

#include <algorithm>
#include <unordered_map>

#include "compiler.hpp"
#include "node.hpp"

namespace jsonschema {

// ---------------------------------------------------------------------------
// schema
// ---------------------------------------------------------------------------

schema::schema(std::unique_ptr<detail::compiled_schema> impl) : impl_(std::move(impl)) {}
schema::schema(schema&&) noexcept = default;
schema& schema::operator=(schema&&) noexcept = default;
schema::~schema() = default;

schema schema::compile(const json& root, compile_options options)
{
    return schema(detail::compile_schema(root, std::move(options)));
}

schema schema::compile(json&& root, compile_options options)
{
    return schema(detail::compile_schema(std::move(root), std::move(options)));
}

bool schema::is_valid(const json& instance) const
{
    detail::eval_context::settings settings;
    settings.track_dynamic_scope = impl_->uses_dynamic_scope;
    detail::eval_context context(settings);
    return detail::evaluate(*impl_->root, instance, context, nullptr, detail::edge::root);
}

namespace {

via to_via(detail::edge e)
{
    switch (e) {
    case detail::edge::root:
        return via::root;
    case detail::edge::child:
        return via::child;
    case detail::edge::ref:
        return via::ref;
    case detail::edge::in_place:
    case detail::edge::none:
        break;
    }
    return via::in_place;
}

/// Delivers classifications in document order (doc 4.3, 4.6).
class classification_delivery
{
public:
    classification_delivery(const std::vector<detail::eval_context::classification_entry>& entries, classification_sink& sink,
                            bool complete_walk)
        : sink_(sink), complete_walk_(complete_walk)
    {
        for (const auto& e : entries) {
            auto& list = by_instance_[e.instance];
            // The same named schema can be reached twice at one location; report it once.
            if (std::none_of(list.begin(), list.end(), [&](const auto* other) { return other->named == e.named; })) {
                list.push_back(&e);
            }
        }
    }

    void walk(const json& value, json::json_pointer& location)
    {
        const auto found = by_instance_.find(&value);
        const bool container = value.is_object() || value.is_array();
        if (found != by_instance_.end() || (complete_walk_ && container)) {
            matches_.clear();
            auto status = classification_status::unclassified;
            if (found != by_instance_.end()) {
                status = classification_status::exact;
                for (const auto* e : found->second) {
                    matches_.push_back(match{e->named->name, e->named->location, to_via(e->reached_by), e->confidence, *e->named->info});
                    status = std::max(status, static_cast<classification_status>(e->confidence));
                }
            }
            sink_.on_classified(classification{location, value, std::span<const match>(matches_), status});
        }
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                location.push_back(it.key());
                walk(it.value(), location);
                location.pop_back();
            }
        } else if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                location.push_back(std::to_string(i));
                walk(value[i], location);
                location.pop_back();
            }
        }
    }

private:
    classification_sink& sink_;
    bool complete_walk_;
    std::unordered_map<const json*, std::vector<const detail::eval_context::classification_entry*>> by_instance_;
    std::vector<match> matches_;
};

} // namespace

result schema::validate(const json& instance, const validation_options& options) const
{
    if (options.classifications != nullptr && !impl_->classification_enabled) {
        throw std::invalid_argument("json-schema-cpp: a classification sink needs compile_options::classification.enabled");
    }
    detail::eval_context::settings settings;
    settings.track_dynamic_scope = impl_->uses_dynamic_scope;
    settings.errors = options.errors;
    settings.max_errors = options.max_errors;
    settings.annotations = options.annotations != nullptr;
    settings.classify = options.classifications != nullptr;
    settings.recover = impl_->recover;
    detail::eval_context context(settings);

    const bool valid = detail::evaluate(*impl_->root, instance, context, nullptr, detail::edge::root);
    context.flush_errors();

    if (options.annotations != nullptr) {
        for (const auto& a : context.annotations_) {
            options.annotations->on_annotation(annotation{a.instance_location, a.evaluation_path, a.schema_location, a.keyword, *a.value});
        }
    }
    if (options.classifications != nullptr) {
        classification_delivery delivery(context.classifications_, *options.classifications, impl_->complete_walk);
        json::json_pointer location;
        delivery.walk(instance, location);
    }
    return result{valid};
}

const uri& schema::base_uri() const noexcept
{
    return impl_->base_uri;
}

schema_error::schema_error(const std::string& message, std::vector<error> metaschema_errors)
    : std::runtime_error(message), metaschema_errors_(std::make_shared<const std::vector<error>>(std::move(metaschema_errors)))
{
}

std::span<const error> schema_error::metaschema_errors() const noexcept
{
    if (!metaschema_errors_) {
        return {};
    }
    return {metaschema_errors_->data(), metaschema_errors_->size()};
}

// ---------------------------------------------------------------------------
// validation.hpp
// ---------------------------------------------------------------------------

validation_error::validation_error(const error& e)
    : std::runtime_error(e.message + " (at " + (e.instance_location.empty() ? std::string("the root") : e.instance_location.to_string()) +
                         ", " + e.evaluation_path + ")"),
      instance_location(e.instance_location),
      evaluation_path(e.evaluation_path),
      schema_location(e.schema_location),
      keyword(e.keyword)
{
}

void throwing_error_sink::on_error(const error& e)
{
    throw validation_error(e);
}

// ---------------------------------------------------------------------------
// classification.hpp
// ---------------------------------------------------------------------------

namer namer::defs_children()
{
    return custom([](const schema_node_view& view) -> std::optional<std::string> {
        if ((view.parent_keyword == "$defs" || view.parent_keyword == "definitions") && !view.key.empty()) {
            return std::string(view.key);
        }
        return std::nullopt;
    });
}

namer namer::anchors()
{
    return custom([](const schema_node_view& view) -> std::optional<std::string> {
        if (view.schema.is_object()) {
            if (const auto it = view.schema.find("$anchor"); it != view.schema.end() && it->is_string()) {
                return it->get<std::string>();
            }
        }
        return std::nullopt;
    });
}

namer namer::custom(rule r)
{
    namer result;
    result.rules_.push_back(std::move(r));
    return result;
}

namer namer::operator|(const namer& other) const
{
    namer result = *this;
    result.rules_.insert(result.rules_.end(), other.rules_.begin(), other.rules_.end());
    return result;
}

std::optional<std::string> namer::operator()(const schema_node_view& view) const
{
    for (const auto& r : rules_) {
        if (auto name = r(view)) {
            return name;
        }
    }
    return std::nullopt;
}

bool schema_info::declares_property(std::string_view name) const
{
    return std::binary_search(declared_.begin(), declared_.end(), name,
                              [](const auto& a, const auto& b) { return std::string_view(a) < std::string_view(b); });
}

} // namespace jsonschema
