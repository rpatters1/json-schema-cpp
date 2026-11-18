// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include "regex.hpp"

#include <regex>
#include <string>

namespace jsonschema::detail {

namespace {

class std_regex final : public compiled_regex
{
public:
    explicit std_regex(std::regex expression) : expression_(std::move(expression)) {}

    [[nodiscard]] bool search(std::string_view utf8) const override
    {
        return std::regex_search(utf8.begin(), utf8.end(), expression_);
    }

private:
    std::regex expression_;
};

} // namespace

std::unique_ptr<compiled_regex> compile_regex(std::string_view pattern)
{
    try {
        return std::make_unique<std_regex>(std::regex(std::string(pattern), std::regex::ECMAScript));
    } catch (const std::regex_error&) {
        return nullptr;
    }
}

} // namespace jsonschema::detail
