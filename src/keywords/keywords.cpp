// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
#include "keywords/keywords.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace jsonschema::detail {

namespace {

// Sorted by name for binary search. Keywords not listed here need no compiling:
// identifiers and anchors (the registry handles them), annotations (`title`, `default`,
// `format` until milestone 4, `content*`, ...), and keywords that a sibling compiles
// (`then`/`else` with `if`, `minContains`/`maxContains` with `contains`).
// `dependencies` is superseded but supported by default (compile_options::legacy_dependencies).
constexpr std::array keyword_table = {
    keyword_definition{"$dynamicRef", keyword_phase::reference, make_dynamic_ref, vocab_core},
    keyword_definition{"$ref", keyword_phase::reference, make_ref, vocab_core},
    keyword_definition{"additionalProperties", keyword_phase::child, make_additional_properties, vocab_applicator},
    keyword_definition{"allOf", keyword_phase::in_place, make_all_of, vocab_applicator},
    keyword_definition{"anyOf", keyword_phase::in_place, make_any_of, vocab_applicator},
    keyword_definition{"const", keyword_phase::assertion, make_const, vocab_validation},
    keyword_definition{"contains", keyword_phase::child, make_contains, vocab_applicator},
    keyword_definition{"dependencies", keyword_phase::in_place, make_dependencies, vocab_applicator},
    keyword_definition{"dependentRequired", keyword_phase::assertion, make_dependent_required, vocab_validation},
    keyword_definition{"dependentSchemas", keyword_phase::in_place, make_dependent_schemas, vocab_applicator},
    keyword_definition{"enum", keyword_phase::assertion, make_enum, vocab_validation},
    keyword_definition{"exclusiveMaximum", keyword_phase::assertion, make_exclusive_maximum, vocab_validation},
    keyword_definition{"exclusiveMinimum", keyword_phase::assertion, make_exclusive_minimum, vocab_validation},
    keyword_definition{"if", keyword_phase::in_place, make_if, vocab_applicator},
    keyword_definition{"items", keyword_phase::child, make_items, vocab_applicator},
    keyword_definition{"maxItems", keyword_phase::assertion, make_max_items, vocab_validation},
    keyword_definition{"maxLength", keyword_phase::assertion, make_max_length, vocab_validation},
    keyword_definition{"maxProperties", keyword_phase::assertion, make_max_properties, vocab_validation},
    keyword_definition{"maximum", keyword_phase::assertion, make_maximum, vocab_validation},
    keyword_definition{"minItems", keyword_phase::assertion, make_min_items, vocab_validation},
    keyword_definition{"minLength", keyword_phase::assertion, make_min_length, vocab_validation},
    keyword_definition{"minProperties", keyword_phase::assertion, make_min_properties, vocab_validation},
    keyword_definition{"minimum", keyword_phase::assertion, make_minimum, vocab_validation},
    keyword_definition{"multipleOf", keyword_phase::assertion, make_multiple_of, vocab_validation},
    keyword_definition{"not", keyword_phase::in_place, make_not, vocab_applicator},
    keyword_definition{"oneOf", keyword_phase::in_place, make_one_of, vocab_applicator},
    keyword_definition{"pattern", keyword_phase::assertion, make_pattern, vocab_validation},
    keyword_definition{"patternProperties", keyword_phase::child, make_pattern_properties, vocab_applicator},
    keyword_definition{"prefixItems", keyword_phase::child, make_prefix_items, vocab_applicator},
    keyword_definition{"properties", keyword_phase::child, make_properties, vocab_applicator},
    keyword_definition{"propertyNames", keyword_phase::child, make_property_names, vocab_applicator},
    keyword_definition{"required", keyword_phase::assertion, make_required, vocab_validation},
    keyword_definition{"type", keyword_phase::assertion, make_type, vocab_validation},
    keyword_definition{"unevaluatedItems", keyword_phase::unevaluated, make_unevaluated_items, vocab_unevaluated},
    keyword_definition{"unevaluatedProperties", keyword_phase::unevaluated, make_unevaluated_properties, vocab_unevaluated},
    keyword_definition{"uniqueItems", keyword_phase::assertion, make_unique_items, vocab_validation},
};

constexpr bool table_is_sorted()
{
    for (std::size_t i = 1; i < keyword_table.size(); ++i) {
        if (!(keyword_table[i - 1].name < keyword_table[i].name)) {
            return false;
        }
    }
    return true;
}
static_assert(table_is_sorted(), "keyword_table must be sorted by name");

} // namespace

unsigned vocabulary_bit(std::string_view uri)
{
    static constexpr std::array<std::pair<std::string_view, unsigned>, 7> known = {{
        {"https://json-schema.org/draft/2020-12/vocab/core", vocab_core},
        {"https://json-schema.org/draft/2020-12/vocab/applicator", vocab_applicator},
        {"https://json-schema.org/draft/2020-12/vocab/unevaluated", vocab_unevaluated},
        {"https://json-schema.org/draft/2020-12/vocab/validation", vocab_validation},
        {"https://json-schema.org/draft/2020-12/vocab/meta-data", vocab_meta_data},
        {"https://json-schema.org/draft/2020-12/vocab/format-annotation", vocab_format_annotation},
        {"https://json-schema.org/draft/2020-12/vocab/content", vocab_content},
    }};
    for (const auto& [name, bit] : known) {
        if (name == uri) {
            return bit;
        }
    }
    return 0;
}

const keyword_definition* find_keyword(std::string_view name)
{
    const auto it = std::lower_bound(keyword_table.begin(), keyword_table.end(), name,
                                     [](const keyword_definition& d, std::string_view n) { return d.name < n; });
    return it != keyword_table.end() && it->name == name ? &*it : nullptr;
}

std::uint64_t non_negative_integer(const compile_scope& scope)
{
    const json& value = scope.value();
    switch (value.type()) {
    case json::value_t::number_unsigned:
        return value.get<std::uint64_t>();
    case json::value_t::number_integer:
        if (value.get<std::int64_t>() >= 0) {
            return static_cast<std::uint64_t>(value.get<std::int64_t>());
        }
        break;
    case json::value_t::number_float: {
        const double d = value.get<double>();
        if (d >= 0 && std::isfinite(d) && std::trunc(d) == d) {
            // 2^64 is exactly representable; anything at or above it saturates.
            return d >= 18446744073709551616.0 ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(d);
        }
        break;
    }
    default:
        break;
    }
    scope.fail("the value must be a non-negative integer");
}

std::vector<const node*> subschema_array(const compile_scope& scope)
{
    const json& value = scope.value();
    if (!value.is_array() || value.empty()) {
        scope.fail("the value must be a non-empty array of schemas");
    }
    std::vector<const node*> result;
    result.reserve(value.size());
    for (const auto& element : value) {
        result.push_back(scope.subschema(element));
    }
    return result;
}

} // namespace jsonschema::detail
