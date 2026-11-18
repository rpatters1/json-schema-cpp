// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Internal JSON access layer (design doc 2.1). The validator core uses only these
// functions to inspect instances and schemas, so that nlohmann::json details stay here.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace jsonschema::detail::json_access {

using json = nlohmann::json;

/// The JSON Schema data model kinds (2020-12 core §4.2.1), excluding "integer",
/// which is a property of numbers rather than a separate kind.
enum class kind
{
    null,
    boolean,
    number,
    string,
    array,
    object,
    unsupported     ///< nlohmann binary or discarded values, which have no JSON Schema meaning
};

[[nodiscard]] inline kind kind_of(const json& value) noexcept
{
    switch (value.type()) {
    case json::value_t::null:
        return kind::null;
    case json::value_t::boolean:
        return kind::boolean;
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
    case json::value_t::number_float:
        return kind::number;
    case json::value_t::string:
        return kind::string;
    case json::value_t::array:
        return kind::array;
    case json::value_t::object:
        return kind::object;
    case json::value_t::binary:
    case json::value_t::discarded:
        return kind::unsupported;
    }
    return kind::unsupported;
}

/// True if @p value is a number with no fractional part (so `1.0` is an integer, per 2020-12).
[[nodiscard]] bool is_integral(const json& value) noexcept;

/// Exact numeric comparison across int64, uint64 and double. Both arguments must be numbers.
/// Returns `unordered` if either is NaN.
[[nodiscard]] std::partial_ordering compare_numbers(const json& a, const json& b) noexcept;

/// JSON Schema equality (2020-12 core §4.2.2): numbers compare by mathematical value,
/// strings by code points, arrays element-wise, objects by member set regardless of order.
[[nodiscard]] bool equal(const json& a, const json& b) noexcept;

/// A hash consistent with `equal`: values that compare equal hash equally (e.g. `1` and `1.0`).
[[nodiscard]] std::size_t hash(const json& value) noexcept;

/// A view of a string value's UTF-8 bytes. @p value must be a string.
[[nodiscard]] inline std::string_view string_view_of(const json& value) noexcept
{
    return value.get_ref<const std::string&>();
}

/// Number of Unicode code points in @p utf8 (for minLength/maxLength). The input is assumed
/// to be well-formed UTF-8, which nlohmann::json's parser guarantees; for ill-formed input
/// the result counts every byte that is not a continuation byte.
[[nodiscard]] std::size_t code_point_count(std::string_view utf8) noexcept;

} // namespace jsonschema::detail::json_access
