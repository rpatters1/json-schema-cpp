// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Checks that an installed json-schema-cpp can be found, linked and used.
#include <iostream>

#include <jsonschema/jsonschema.hpp>

int main()
{
    if (jsonschema::version() != JSONSCHEMA_VERSION_STRING) {
        std::cerr << "library version " << jsonschema::version() << " does not match headers "
                  << JSONSCHEMA_VERSION_STRING << "\n";
        return 1;
    }
    const auto schema = jsonschema::schema::compile(nlohmann::json::parse(R"({
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "properties": {"name": {"type": "string", "pattern": "^\\p{Lu}"}},
        "required": ["name"]
    })"));
    if (!schema.is_valid(nlohmann::json{{"name", "Émile"}}) ||
        schema.is_valid(nlohmann::json{{"name", "émile"}}) ||
        schema.is_valid(nlohmann::json::object())) {
        std::cerr << "unexpected validation result\n";
        return 1;
    }
    std::cout << "json-schema-cpp " << jsonschema::version() << ": ok\n";
    return 0;
}
