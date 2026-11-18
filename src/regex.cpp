// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "regex.hpp"

#include <string>

#include <jsonschema/schema.hpp>

#if defined(JSONSCHEMA_REGEX_SRELL)
#include <srell.hpp>
#else
#include <regex>
#endif

namespace jsonschema::detail {

namespace {

#if defined(JSONSCHEMA_REGEX_SRELL)

/// SRELL with UTF-8 input (`u8cregex`): patterns and strings are matched by code point,
/// as ECMA-262 does with the "u" flag.
class srell_regex final : public compiled_regex
{
public:
    explicit srell_regex(srell::u8cregex expression, std::string pattern)
        : expression_(std::move(expression)), pattern_(std::move(pattern))
    {
    }

    [[nodiscard]] bool search(std::string_view utf8) const override
    {
        try {
            return srell::regex_search(utf8.data(), utf8.data() + utf8.size(), expression_);
        } catch (const srell::regex_error& e) {
            // The backtracking limit (error_complexity) or the stack limit: the result is
            // indeterminate, not "no match".
            throw evaluation_error("json-schema-cpp: the regular expression \"" + pattern_ +
                                   "\" is too complex to evaluate on this string (" + e.what() + ")");
        }
    }

private:
    srell::u8cregex expression_;
    std::string pattern_;
};

#else

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

#endif

/// True if @p pattern has a global inline flag group such as "(?i)" or "(?ims)". Engines
/// such as SRELL and PCRE accept these, but ECMA-262 does not. Its modifier groups
/// "(?i:...)" and "(?-i:...)" (ES2025) are valid and are not matched here.
bool has_global_inline_flags(std::string_view pattern) noexcept
{
    bool in_class = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\\') {
            ++i;    // skip the escaped character
        } else if (in_class) {
            in_class = c != ']';
        } else if (c == '[') {
            in_class = true;
        } else if (c == '(' && i + 1 < pattern.size() && pattern[i + 1] == '?') {
            std::size_t j = i + 2;
            while (j < pattern.size() && std::string_view("imsx-").find(pattern[j]) != std::string_view::npos) {
                ++j;
            }
            if (j > i + 2 && j < pattern.size() && pattern[j] == ')') {
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::unique_ptr<compiled_regex> compile_regex(std::string_view pattern)
{
    if (has_global_inline_flags(pattern)) {
        return nullptr;
    }
#if defined(JSONSCHEMA_REGEX_SRELL)
    try {
        srell::u8cregex expression(pattern.data(), pattern.size(), srell::regex_constants::ECMAScript);
        return std::make_unique<srell_regex>(std::move(expression), std::string(pattern));
    } catch (const srell::regex_error&) {
        return nullptr;
    }
#else
    try {
        return std::make_unique<std_regex>(std::regex(std::string(pattern), std::regex::ECMAScript));
    } catch (const std::regex_error&) {
        return nullptr;
    }
#endif
}

} // namespace jsonschema::detail
