// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <jsonschema/schema.hpp>

#include <algorithm>

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
    settings.classifications = options.classifications;
    settings.check_all_matches = options.classifications != nullptr && options.check_all_matches;
    settings.recover = impl_->recover;
    detail::eval_context context(settings);

    const bool valid = detail::evaluate(*impl_->root, instance, context, nullptr, detail::edge::root);
    context.flush_errors();

    if (options.annotations != nullptr) {
        for (const auto& a : context.annotations_) {
            options.annotations->on_annotation(annotation{a.instance_location, a.evaluation_path, a.schema_location, a.keyword, *a.value});
        }
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
