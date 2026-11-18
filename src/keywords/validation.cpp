// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Validation vocabulary (2020-12 validation §6).
#include <cmath>
#include <compare>
#include <limits>
#include <optional>
#include <unordered_map>

#include "json/access.hpp"
#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace ja = json_access;

namespace {

std::string in_quotes(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

// ---------------------------------------------------------------------------
// Any instance type
// ---------------------------------------------------------------------------

class type_keyword final : public keyword
{
public:
    enum : unsigned
    {
        null_bit = 1u << 0,
        boolean_bit = 1u << 1,
        object_bit = 1u << 2,
        array_bit = 1u << 3,
        number_bit = 1u << 4,
        string_bit = 1u << 5,
        integer_bit = 1u << 6,
    };

    type_keyword(unsigned allowed, std::string expected) : allowed_(allowed), expected_(std::move(expected)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (matches(instance)) {
            return true;
        }
        context.identity_failure();
        context.fail(*this, instance, [&] { return "expected " + expected_ + ", got " + std::string(type_name(instance)); });
        return false;
    }

private:
    [[nodiscard]] bool matches(const json& instance) const
    {
        switch (ja::kind_of(instance)) {
        case ja::kind::null:
            return (allowed_ & null_bit) != 0;
        case ja::kind::boolean:
            return (allowed_ & boolean_bit) != 0;
        case ja::kind::object:
            return (allowed_ & object_bit) != 0;
        case ja::kind::array:
            return (allowed_ & array_bit) != 0;
        case ja::kind::string:
            return (allowed_ & string_bit) != 0;
        case ja::kind::number:
            return (allowed_ & number_bit) != 0 || ((allowed_ & integer_bit) != 0 && ja::is_integral(instance));
        case ja::kind::unsupported:
            return false;
        }
        return false;
    }

    unsigned allowed_;
    std::string expected_;  // "string", or "string or null"
};

class enum_keyword final : public keyword
{
public:
    explicit enum_keyword(std::vector<json> values) : values_(std::move(values)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        for (const auto& value : values_) {
            if (ja::equal(instance, value)) {
                return true;
            }
        }
        context.identity_failure();
        context.fail(*this, instance, [&] {
            std::string text = "the value " + brief(instance) + " is not one of the allowed values";
            if (values_.size() <= 6) {
                text += " (";
                for (std::size_t i = 0; i < values_.size(); ++i) {
                    text += (i == 0 ? "" : ", ") + brief(values_[i]);
                }
                text += ")";
            }
            return text;
        });
        return false;
    }

private:
    std::vector<json> values_;
};

class const_keyword final : public keyword
{
public:
    explicit const_keyword(json value) : value_(std::move(value)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (ja::equal(instance, value_)) {
            return true;
        }
        context.identity_failure();
        context.fail(*this, instance, [&] { return "the value " + brief(instance) + " is not " + brief(value_); });
        return false;
    }

private:
    json value_;
};

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

constexpr double two_pow_53 = 9007199254740992.0;
constexpr double two_pow_63 = 9223372036854775808.0;

/// |value| for an integer-typed json number.
std::uint64_t magnitude(const json& value)
{
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const std::int64_t i = value.get<std::int64_t>();
    return i < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(i) : static_cast<std::uint64_t>(i);
}

/// Doc 5.3: exact for integers, then a double quotient, then `fmod` on overflow.
bool is_multiple_of(const json& instance, const json& divisor)
{
    // Exact integer path. A float divisor qualifies if it is a whole number below 2^63.
    if (instance.is_number_integer()) {
        std::optional<std::uint64_t> exact_divisor;
        if (divisor.is_number_integer()) {
            exact_divisor = magnitude(divisor);
        } else {
            const double d = divisor.get<double>();
            if (std::trunc(d) == d && d < two_pow_63) {
                exact_divisor = static_cast<std::uint64_t>(d);
            }
        }
        if (exact_divisor) {
            return magnitude(instance) % *exact_divisor == 0;
        }
    }

    const double x = instance.get<double>();
    const double d = divisor.get<double>();
    const double quotient = x / d;
    if (!std::isfinite(quotient)) {
        return std::fmod(x, d) == 0.0;
    }
    const double magnitude_q = std::fabs(quotient);
    if (magnitude_q >= two_pow_53) {
        // Whole-number operands: fmod is exact, so `1e20` is correctly not a multiple of 3.
        // Otherwise the quotient is beyond double precision and counts as whole, as it would
        // for any implementation working in doubles.
        return std::trunc(d) == d && std::trunc(x) == x ? std::fmod(x, d) == 0.0 : true;
    }
    // Allow a few units in the last place, so that 0.0075 is a multiple of 0.0001.
    return std::fabs(quotient - std::round(quotient)) <= 4.0 * std::numeric_limits<double>::epsilon() * magnitude_q;
}

class multiple_of_keyword final : public keyword
{
public:
    explicit multiple_of_keyword(json divisor) : divisor_(std::move(divisor)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_number() || is_multiple_of(instance, divisor_)) {
            return true;
        }
        context.fail(*this, instance, [&] { return instance.dump() + " is not a multiple of " + divisor_.dump(); });
        return false;
    }

private:
    json divisor_;
};

enum class bound
{
    maximum,
    exclusive_maximum,
    minimum,
    exclusive_minimum
};

template <bound kind>
class bound_keyword final : public keyword
{
public:
    explicit bound_keyword(json limit) : limit_(std::move(limit)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_number()) {
            return true;
        }
        const std::partial_ordering order = ja::compare_numbers(instance, limit_);
        bool valid;
        std::string_view relation;
        if constexpr (kind == bound::maximum) {
            valid = order == std::partial_ordering::less || order == std::partial_ordering::equivalent;
            relation = "greater than the maximum";
        } else if constexpr (kind == bound::exclusive_maximum) {
            valid = order == std::partial_ordering::less;
            relation = "not less than the exclusive maximum";
        } else if constexpr (kind == bound::minimum) {
            valid = order == std::partial_ordering::greater || order == std::partial_ordering::equivalent;
            relation = "less than the minimum";
        } else {
            valid = order == std::partial_ordering::greater;
            relation = "not greater than the exclusive minimum";
        }
        if (!valid) {
            context.fail(*this, instance, [&] { return instance.dump() + " is " + std::string(relation) + " " + limit_.dump(); });
        }
        return valid;
    }

private:
    json limit_;
};

const json& number_value(const compile_scope& scope)
{
    if (!scope.value().is_number()) {
        scope.fail("the value must be a number");
    }
    return scope.value();
}

// ---------------------------------------------------------------------------
// Strings, arrays, objects: size limits
// ---------------------------------------------------------------------------

enum class size_of
{
    string_code_points,
    array_items,
    object_members
};

template <size_of measure, bool is_maximum>
class size_keyword final : public keyword
{
public:
    explicit size_keyword(std::uint64_t limit) : limit_(limit) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        std::uint64_t size = 0;
        std::string_view what;
        std::string_view unit;
        if constexpr (measure == size_of::string_code_points) {
            if (!instance.is_string()) {
                return true;
            }
            size = ja::code_point_count(ja::string_view_of(instance));
            what = "string";
            unit = "characters";
        } else if constexpr (measure == size_of::array_items) {
            if (!instance.is_array()) {
                return true;
            }
            size = instance.size();
            what = "array";
            unit = "items";
        } else {
            if (!instance.is_object()) {
                return true;
            }
            size = instance.size();
            what = "object";
            unit = "properties";
        }
        bool valid;
        if constexpr (is_maximum) {
            valid = size <= limit_;
        } else {
            valid = size >= limit_;
        }
        if (!valid) {
            context.fail(*this, instance, [&] {
                return "the " + std::string(what) + " has " + std::to_string(size) + " " + std::string(unit) + ", " +
                       (is_maximum ? "more than the maximum of " : "fewer than the minimum of ") + std::to_string(limit_);
            });
        }
        return valid;
    }

private:
    std::uint64_t limit_;
};

template <size_of measure, bool is_maximum>
std::unique_ptr<keyword> make_size(const compile_scope& scope)
{
    const std::uint64_t limit = non_negative_integer(scope);
    if constexpr (!is_maximum) {
        if (limit == 0) {
            return nullptr;     // a minimum of zero always holds
        }
    }
    return std::make_unique<size_keyword<measure, is_maximum>>(limit);
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

class pattern_keyword final : public keyword
{
public:
    pattern_keyword(std::string text, std::shared_ptr<const compiled_regex> pattern) : text_(std::move(text)), pattern_(std::move(pattern)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_string() || pattern_->search(ja::string_view_of(instance))) {
            return true;
        }
        context.fail(*this, instance, [&] { return "the string " + brief(instance) + " does not match the pattern " + in_quotes(text_); });
        return false;
    }

private:
    std::string text_;
    std::shared_ptr<const compiled_regex> pattern_;
};

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

class unique_items_keyword final : public keyword
{
public:
    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_array() || instance.size() < 2) {
            return true;
        }
        const auto duplicate = find_duplicate(instance);
        if (!duplicate) {
            return true;
        }
        context.fail(*this, instance, [&] {
            return "the array items " + std::to_string(duplicate->first) + " and " + std::to_string(duplicate->second) + " are equal";
        });
        return false;
    }

private:
    static std::optional<std::pair<std::size_t, std::size_t>> find_duplicate(const json& instance)
    {
        constexpr std::size_t quadratic_limit = 16;
        if (instance.size() <= quadratic_limit) {
            for (std::size_t i = 1; i < instance.size(); ++i) {
                for (std::size_t j = 0; j < i; ++j) {
                    if (ja::equal(instance[i], instance[j])) {
                        return std::pair{j, i};
                    }
                }
            }
            return std::nullopt;
        }
        std::unordered_multimap<std::size_t, std::size_t> seen;
        seen.reserve(instance.size());
        for (std::size_t i = 0; i < instance.size(); ++i) {
            const std::size_t h = ja::hash(instance[i]);
            const auto [first, last] = seen.equal_range(h);
            for (auto it = first; it != last; ++it) {
                if (ja::equal(instance[i], instance[it->second])) {
                    return std::pair{it->second, i};
                }
            }
            seen.emplace(h, i);
        }
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

std::vector<std::string> string_array(const compile_scope& scope, const json& value)
{
    if (!value.is_array()) {
        scope.fail("the value must be an array of strings");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const auto& element : value) {
        if (!element.is_string()) {
            scope.fail("the value must be an array of strings");
        }
        result.push_back(element.get<std::string>());
    }
    return result;
}

class required_keyword final : public keyword
{
public:
    struct name_entry
    {
        std::string name;
        bool discriminator;     // a missing discriminator is an identity failure (doc 4.6)
    };

    explicit required_keyword(std::vector<name_entry> names) : names_(std::move(names)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        for (const auto& entry : names_) {
            if (instance.contains(entry.name)) {
                continue;
            }
            valid = false;
            if (entry.discriminator) {
                context.identity_failure();
            }
            context.fail(*this, instance, [&] { return "the required property " + in_quotes(entry.name) + " is missing"; });
            if (!context.exhaustive()) {
                break;
            }
        }
        return valid;
    }

private:
    std::vector<name_entry> names_;
};

class dependent_required_keyword final : public keyword
{
public:
    explicit dependent_required_keyword(std::vector<std::pair<std::string, std::vector<std::string>>> dependencies)
        : dependencies_(std::move(dependencies))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        for (const auto& [name, required] : dependencies_) {
            if (!instance.contains(name)) {
                continue;
            }
            for (const auto& other : required) {
                if (instance.contains(other)) {
                    continue;
                }
                valid = false;
                context.fail(*this, instance, [&] {
                    return "the property " + in_quotes(name) + " requires the property " + in_quotes(other) + ", which is missing";
                });
                if (!context.exhaustive()) {
                    return false;
                }
            }
        }
        return valid;
    }

private:
    std::vector<std::pair<std::string, std::vector<std::string>>> dependencies_;
};

/// True if the schema's `properties` gives @p name a `const` or single-value `enum` subschema.
bool is_discriminator(const compile_scope& scope, const std::string& name)
{
    const json* properties = scope.sibling("properties");
    if (properties == nullptr || !properties->is_object()) {
        return false;
    }
    const auto it = properties->find(name);
    if (it == properties->end() || !it->is_object()) {
        return false;
    }
    if (it->contains("const")) {
        return true;
    }
    const auto values = it->find("enum");
    return values != it->end() && values->is_array() && values->size() == 1;
}

} // namespace

std::unique_ptr<keyword> make_type(const compile_scope& scope)
{
    const auto bit_for = [&](const json& name) -> unsigned {
        if (name.is_string()) {
            const std::string& text = name.get_ref<const std::string&>();
            if (text == "null") return type_keyword::null_bit;
            if (text == "boolean") return type_keyword::boolean_bit;
            if (text == "object") return type_keyword::object_bit;
            if (text == "array") return type_keyword::array_bit;
            if (text == "number") return type_keyword::number_bit;
            if (text == "string") return type_keyword::string_bit;
            if (text == "integer") return type_keyword::integer_bit;
        }
        scope.fail("the value must be a type name or an array of type names");
    };
    unsigned allowed = 0;
    std::string expected;
    if (scope.value().is_array()) {
        const auto& names = scope.value();
        for (std::size_t i = 0; i < names.size(); ++i) {
            const unsigned bit = bit_for(names[i]);
            if ((allowed & bit) != 0) {
                scope.fail("type names must be unique");
            }
            allowed |= bit;
            expected += (i == 0 ? "" : (i + 1 == names.size() ? " or " : ", ")) + names[i].get<std::string>();
        }
    } else {
        allowed = bit_for(scope.value());
        expected = scope.value().get<std::string>();
    }
    return std::make_unique<type_keyword>(allowed, expected.empty() ? std::string("no type at all") : expected);
}

std::unique_ptr<keyword> make_enum(const compile_scope& scope)
{
    if (!scope.value().is_array()) {
        scope.fail("the value must be an array");
    }
    return std::make_unique<enum_keyword>(scope.value().get<std::vector<json>>());
}

std::unique_ptr<keyword> make_const(const compile_scope& scope)
{
    return std::make_unique<const_keyword>(scope.value());
}

std::unique_ptr<keyword> make_multiple_of(const compile_scope& scope)
{
    const json& divisor = number_value(scope);
    if (!(divisor.get<double>() > 0.0)) {
        scope.fail("the value must be greater than 0");
    }
    return std::make_unique<multiple_of_keyword>(divisor);
}

std::unique_ptr<keyword> make_maximum(const compile_scope& scope)
{
    return std::make_unique<bound_keyword<bound::maximum>>(number_value(scope));
}

std::unique_ptr<keyword> make_exclusive_maximum(const compile_scope& scope)
{
    return std::make_unique<bound_keyword<bound::exclusive_maximum>>(number_value(scope));
}

std::unique_ptr<keyword> make_minimum(const compile_scope& scope)
{
    return std::make_unique<bound_keyword<bound::minimum>>(number_value(scope));
}

std::unique_ptr<keyword> make_exclusive_minimum(const compile_scope& scope)
{
    return std::make_unique<bound_keyword<bound::exclusive_minimum>>(number_value(scope));
}

std::unique_ptr<keyword> make_max_length(const compile_scope& scope)
{
    return make_size<size_of::string_code_points, true>(scope);
}

std::unique_ptr<keyword> make_min_length(const compile_scope& scope)
{
    return make_size<size_of::string_code_points, false>(scope);
}

std::unique_ptr<keyword> make_pattern(const compile_scope& scope)
{
    if (!scope.value().is_string()) {
        scope.fail("the value must be a string");
    }
    const std::string& text = scope.value().get_ref<const std::string&>();
    return std::make_unique<pattern_keyword>(text, scope.regex(text));
}

std::unique_ptr<keyword> make_max_items(const compile_scope& scope)
{
    return make_size<size_of::array_items, true>(scope);
}

std::unique_ptr<keyword> make_min_items(const compile_scope& scope)
{
    return make_size<size_of::array_items, false>(scope);
}

std::unique_ptr<keyword> make_unique_items(const compile_scope& scope)
{
    if (!scope.value().is_boolean()) {
        scope.fail("the value must be a boolean");
    }
    if (!scope.value().get<bool>()) {
        return nullptr;
    }
    return std::make_unique<unique_items_keyword>();
}

std::unique_ptr<keyword> make_max_properties(const compile_scope& scope)
{
    return make_size<size_of::object_members, true>(scope);
}

std::unique_ptr<keyword> make_min_properties(const compile_scope& scope)
{
    return make_size<size_of::object_members, false>(scope);
}

std::unique_ptr<keyword> make_required(const compile_scope& scope)
{
    auto names = string_array(scope, scope.value());
    if (names.empty()) {
        return nullptr;
    }
    std::vector<required_keyword::name_entry> entries;
    entries.reserve(names.size());
    for (auto& name : names) {
        const bool discriminator = is_discriminator(scope, name);
        entries.push_back({std::move(name), discriminator});
    }
    return std::make_unique<required_keyword>(std::move(entries));
}

std::unique_ptr<keyword> make_dependent_required(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are arrays of strings");
    }
    std::vector<std::pair<std::string, std::vector<std::string>>> dependencies;
    for (const auto& [name, value] : scope.value().items()) {
        dependencies.emplace_back(name, string_array(scope, value));
    }
    return std::make_unique<dependent_required_keyword>(std::move(dependencies));
}

} // namespace jsonschema::detail
