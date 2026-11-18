// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include <jsonschema/schema.hpp>

namespace jsonschema {

namespace detail {
class compiled_schema
{
};
} // namespace detail

schema::schema(std::unique_ptr<detail::compiled_schema> impl) : impl_(std::move(impl)) {}
schema::schema(schema&&) noexcept = default;
schema& schema::operator=(schema&&) noexcept = default;
schema::~schema() = default;

schema schema::compile(const json& /*root*/, compile_options /*options*/)
{
    throw schema_error("json-schema-cpp: schema compilation is not implemented yet (milestone 1)");
}

bool schema::is_valid(const json& /*instance*/) const
{
    throw schema_error("json-schema-cpp: validation is not implemented yet (milestone 1)");
}

} // namespace jsonschema
