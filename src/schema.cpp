// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include <jsonschema/schema.hpp>

#include "compiler.hpp"
#include "node.hpp"

namespace jsonschema {

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
    detail::eval_context context(impl_->uses_dynamic_scope);
    return detail::evaluate(*impl_->root, instance, context);
}

const uri& schema::base_uri() const noexcept
{
    return impl_->base_uri;
}

} // namespace jsonschema
