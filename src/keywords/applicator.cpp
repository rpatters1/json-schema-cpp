// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// Applicator vocabulary, except `unevaluated*` (milestone 2).
#include <algorithm>
#include <optional>
#include <unordered_set>

#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace {

// ---------------------------------------------------------------------------
// In-place applicators
// ---------------------------------------------------------------------------

class all_of_keyword final : public keyword
{
public:
    explicit all_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        return std::all_of(subschemas_.begin(), subschemas_.end(),
                           [&](const node* s) { return detail::evaluate(*s, instance, context); });
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

class any_of_keyword final : public keyword
{
public:
    explicit any_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        return std::any_of(subschemas_.begin(), subschemas_.end(),
                           [&](const node* s) { return detail::evaluate(*s, instance, context); });
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

class one_of_keyword final : public keyword
{
public:
    explicit one_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        std::size_t passed = 0;
        for (const node* s : subschemas_) {
            if (detail::evaluate(*s, instance, context) && ++passed > 1) {
                return false;
            }
        }
        return passed == 1;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

class not_keyword final : public keyword
{
public:
    explicit not_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        return !detail::evaluate(*subschema_, instance, context);
    }
    void in_place_subschemas(std::vector<const node*>& out) const override { out.push_back(subschema_); }

private:
    const node* subschema_;
};

class if_keyword final : public keyword
{
public:
    if_keyword(const node* condition, const node* then_branch, const node* else_branch)
        : condition_(condition), then_(then_branch), else_(else_branch)
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        const node* branch = detail::evaluate(*condition_, instance, context) ? then_ : else_;
        return branch == nullptr || detail::evaluate(*branch, instance, context);
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        for (const node* n : {condition_, then_, else_}) {
            if (n != nullptr) {
                out.push_back(n);
            }
        }
    }

private:
    const node* condition_;
    const node* then_;
    const node* else_;
};

class dependent_schemas_keyword final : public keyword
{
public:
    explicit dependent_schemas_keyword(std::vector<std::pair<std::string, const node*>> dependencies)
        : dependencies_(std::move(dependencies))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        for (const auto& [name, subschema] : dependencies_) {
            if (instance.contains(name) && !detail::evaluate(*subschema, instance, context)) {
                return false;
            }
        }
        return true;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        for (const auto& entry : dependencies_) {
            out.push_back(entry.second);
        }
    }

private:
    std::vector<std::pair<std::string, const node*>> dependencies_;
};

// ---------------------------------------------------------------------------
// Child applicators: objects
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, const node*>> subschema_map(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas");
    }
    std::vector<std::pair<std::string, const node*>> result;
    result.reserve(scope.value().size());
    for (const auto& [name, value] : scope.value().items()) {
        result.emplace_back(name, scope.subschema(value));
    }
    return result;
}

class properties_keyword final : public keyword
{
public:
    explicit properties_keyword(std::vector<std::pair<std::string, const node*>> properties) : properties_(std::move(properties)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        for (const auto& [name, subschema] : properties_) {
            const auto it = instance.find(name);
            if (it != instance.end() && !detail::evaluate(*subschema, *it, context)) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<std::pair<std::string, const node*>> properties_;
};

using pattern_list = std::vector<std::pair<std::shared_ptr<const compiled_regex>, const node*>>;

pattern_list compile_pattern_map(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas");
    }
    pattern_list result;
    for (const auto& [pattern, value] : scope.value().items()) {
        result.emplace_back(scope.regex(pattern), scope.subschema(value));
    }
    return result;
}

class pattern_properties_keyword final : public keyword
{
public:
    explicit pattern_properties_keyword(pattern_list patterns) : patterns_(std::move(patterns)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        for (const auto& [name, value] : instance.items()) {
            for (const auto& [pattern, subschema] : patterns_) {
                if (pattern->search(name) && !detail::evaluate(*subschema, value, context)) {
                    return false;
                }
            }
        }
        return true;
    }

private:
    pattern_list patterns_;
};

class additional_properties_keyword final : public keyword
{
public:
    additional_properties_keyword(const node* subschema, std::unordered_set<std::string> names,
                                  std::vector<std::shared_ptr<const compiled_regex>> patterns)
        : subschema_(subschema), names_(std::move(names)), patterns_(std::move(patterns))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        for (const auto& [name, value] : instance.items()) {
            if (names_.contains(name) ||
                std::any_of(patterns_.begin(), patterns_.end(), [&](const auto& p) { return p->search(name); })) {
                continue;
            }
            if (!detail::evaluate(*subschema_, value, context)) {
                return false;
            }
        }
        return true;
    }

private:
    const node* subschema_;
    std::unordered_set<std::string> names_;
    std::vector<std::shared_ptr<const compiled_regex>> patterns_;
};

class property_names_keyword final : public keyword
{
public:
    explicit property_names_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        for (const auto& [name, value] : instance.items()) {
            static_cast<void>(value);
            if (!detail::evaluate(*subschema_, json(name), context)) {
                return false;
            }
        }
        return true;
    }

private:
    const node* subschema_;
};

// ---------------------------------------------------------------------------
// Child applicators: arrays
// ---------------------------------------------------------------------------

class prefix_items_keyword final : public keyword
{
public:
    explicit prefix_items_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        const std::size_t count = std::min(subschemas_.size(), instance.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (!detail::evaluate(*subschemas_[i], instance[i], context)) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<const node*> subschemas_;
};

class items_keyword final : public keyword
{
public:
    items_keyword(const node* subschema, std::size_t start) : subschema_(subschema), start_(start) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        for (std::size_t i = start_; i < instance.size(); ++i) {
            if (!detail::evaluate(*subschema_, instance[i], context)) {
                return false;
            }
        }
        return true;
    }

private:
    const node* subschema_;
    std::size_t start_;
};

class contains_keyword final : public keyword
{
public:
    contains_keyword(const node* subschema, std::uint64_t minimum, std::optional<std::uint64_t> maximum)
        : subschema_(subschema), minimum_(minimum), maximum_(maximum)
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        std::uint64_t matches = 0;
        for (const auto& item : instance) {
            if (detail::evaluate(*subschema_, item, context)) {
                ++matches;
                if (maximum_ ? matches > *maximum_ : matches >= minimum_) {
                    break;  // the outcome can no longer change
                }
            }
        }
        return matches >= minimum_ && (!maximum_ || matches <= *maximum_);
    }

private:
    const node* subschema_;
    std::uint64_t minimum_;
    std::optional<std::uint64_t> maximum_;
};

} // namespace

std::unique_ptr<keyword> make_all_of(const compile_scope& scope)
{
    return std::make_unique<all_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_any_of(const compile_scope& scope)
{
    return std::make_unique<any_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_one_of(const compile_scope& scope)
{
    return std::make_unique<one_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_not(const compile_scope& scope)
{
    return std::make_unique<not_keyword>(scope.subschema(scope.value()));
}

std::unique_ptr<keyword> make_if(const compile_scope& scope)
{
    const json* then_value = scope.sibling("then");
    const json* else_value = scope.sibling("else");
    const node* condition = scope.subschema(scope.value());
    if (then_value == nullptr && else_value == nullptr) {
        return nullptr;     // `if` alone has no effect on validity
    }
    return std::make_unique<if_keyword>(condition, then_value ? scope.subschema(*then_value) : nullptr,
                                        else_value ? scope.subschema(*else_value) : nullptr);
}

std::unique_ptr<keyword> make_dependent_schemas(const compile_scope& scope)
{
    return std::make_unique<dependent_schemas_keyword>(subschema_map(scope));
}

std::unique_ptr<keyword> make_properties(const compile_scope& scope)
{
    return std::make_unique<properties_keyword>(subschema_map(scope));
}

std::unique_ptr<keyword> make_pattern_properties(const compile_scope& scope)
{
    return std::make_unique<pattern_properties_keyword>(compile_pattern_map(scope));
}

std::unique_ptr<keyword> make_additional_properties(const compile_scope& scope)
{
    const node* subschema = scope.subschema(scope.value());
    std::unordered_set<std::string> names;
    if (const json* properties = scope.sibling("properties"); properties != nullptr && properties->is_object()) {
        for (const auto& [name, value] : properties->items()) {
            static_cast<void>(value);
            names.insert(name);
        }
    }
    std::vector<std::shared_ptr<const compiled_regex>> patterns;
    if (const json* pattern_properties = scope.sibling("patternProperties");
        pattern_properties != nullptr && pattern_properties->is_object()) {
        const compile_scope pattern_scope = scope.for_sibling("patternProperties", *pattern_properties);
        for (const auto& [pattern, value] : pattern_properties->items()) {
            static_cast<void>(value);
            patterns.push_back(pattern_scope.regex(pattern));
        }
    }
    return std::make_unique<additional_properties_keyword>(subschema, std::move(names), std::move(patterns));
}

std::unique_ptr<keyword> make_property_names(const compile_scope& scope)
{
    return std::make_unique<property_names_keyword>(scope.subschema(scope.value()));
}

std::unique_ptr<keyword> make_prefix_items(const compile_scope& scope)
{
    return std::make_unique<prefix_items_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_items(const compile_scope& scope)
{
    if (scope.value().is_array()) {
        scope.fail("in 2020-12, items takes a single schema; use prefixItems for per-position schemas");
    }
    std::size_t start = 0;
    if (const json* prefix = scope.sibling("prefixItems"); prefix != nullptr && prefix->is_array()) {
        start = prefix->size();
    }
    return std::make_unique<items_keyword>(scope.subschema(scope.value()), start);
}

std::unique_ptr<keyword> make_contains(const compile_scope& scope)
{
    const node* subschema = scope.subschema(scope.value());
    const auto limit = [&](std::string_view name) -> std::optional<std::uint64_t> {
        const json* value = scope.sibling(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        return non_negative_integer(scope.for_sibling(name, *value));
    };
    const std::uint64_t minimum = limit("minContains").value_or(1);
    const std::optional<std::uint64_t> maximum = limit("maxContains");
    if (minimum == 0 && !maximum) {
        return nullptr;     // always satisfied
    }
    return std::make_unique<contains_keyword>(subschema, minimum, maximum);
}

} // namespace jsonschema::detail
