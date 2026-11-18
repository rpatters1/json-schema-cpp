// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "json/access.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

namespace jsonschema::detail::json_access {

namespace {

// 2^63 and 2^64 are exactly representable as doubles.
constexpr double two_pow_63 = 9223372036854775808.0;
constexpr double two_pow_64 = 18446744073709551616.0;

std::partial_ordering compare_int_double(std::int64_t i, double d) noexcept
{
    if (std::isnan(d)) {
        return std::partial_ordering::unordered;
    }
    if (d >= two_pow_63) {
        return std::partial_ordering::less;
    }
    if (d < -two_pow_63) {
        return std::partial_ordering::greater;
    }
    const double whole = std::trunc(d);
    const auto whole_int = static_cast<std::int64_t>(whole);
    if (i != whole_int) {
        return i < whole_int ? std::partial_ordering::less : std::partial_ordering::greater;
    }
    const double fraction = d - whole;
    if (fraction > 0) {
        return std::partial_ordering::less;
    }
    if (fraction < 0) {
        return std::partial_ordering::greater;
    }
    return std::partial_ordering::equivalent;
}

std::partial_ordering compare_uint_double(std::uint64_t u, double d) noexcept
{
    if (std::isnan(d)) {
        return std::partial_ordering::unordered;
    }
    if (d < 0) {
        return std::partial_ordering::greater;
    }
    if (d >= two_pow_64) {
        return std::partial_ordering::less;
    }
    const double whole = std::trunc(d);
    const auto whole_uint = static_cast<std::uint64_t>(whole);
    if (u != whole_uint) {
        return u < whole_uint ? std::partial_ordering::less : std::partial_ordering::greater;
    }
    return d > whole ? std::partial_ordering::less : std::partial_ordering::equivalent;
}

std::partial_ordering compare_int_uint(std::int64_t i, std::uint64_t u) noexcept
{
    if (i < 0) {
        return std::partial_ordering::less;
    }
    const auto as_unsigned = static_cast<std::uint64_t>(i);
    return as_unsigned <=> u;
}

std::partial_ordering reverse(std::partial_ordering order) noexcept
{
    if (order == std::partial_ordering::less) {
        return std::partial_ordering::greater;
    }
    if (order == std::partial_ordering::greater) {
        return std::partial_ordering::less;
    }
    return order;
}

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6) + (seed >> 2);
}

enum hash_tag : std::size_t
{
    tag_null = 0x6e756c6c,
    tag_true = 0x74727565,
    tag_false = 0x66616c73,
    tag_nonnegative_integer = 0x2b696e74,
    tag_negative_integer = 0x2d696e74,
    tag_double = 0x64626c65,
    tag_string = 0x73747269,
    tag_array = 0x61727261,
    tag_object = 0x6f626a65,
    tag_unsupported = 0x756e7375
};

std::size_t hash_double(double d) noexcept
{
    // Integral values inside the 64-bit integer range hash as integers, so 1.0 == 1 hashes alike.
    if (std::isfinite(d) && std::trunc(d) == d) {
        if (d >= 0 && d < two_pow_64) {
            std::size_t seed = tag_nonnegative_integer;
            hash_combine(seed, std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(d)));
            return seed;
        }
        if (d < 0 && d >= -two_pow_63) {
            std::size_t seed = tag_negative_integer;
            hash_combine(seed, std::hash<std::int64_t>{}(static_cast<std::int64_t>(d)));
            return seed;
        }
    }
    std::size_t seed = tag_double;
    hash_combine(seed, std::hash<double>{}(d));
    return seed;
}

} // namespace

bool is_integral(const json& value) noexcept
{
    switch (value.type()) {
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        return true;
    case json::value_t::number_float: {
        const double d = value.get<double>();
        return std::isfinite(d) && std::trunc(d) == d;
    }
    default:
        return false;
    }
}

std::partial_ordering compare_numbers(const json& a, const json& b) noexcept
{
    using vt = json::value_t;
    const vt ta = a.type();
    const vt tb = b.type();

    if (ta == vt::number_float && tb == vt::number_float) {
        return a.get<double>() <=> b.get<double>();
    }
    if (ta == vt::number_integer && tb == vt::number_integer) {
        return a.get<std::int64_t>() <=> b.get<std::int64_t>();
    }
    if (ta == vt::number_unsigned && tb == vt::number_unsigned) {
        return a.get<std::uint64_t>() <=> b.get<std::uint64_t>();
    }
    if (ta == vt::number_integer && tb == vt::number_unsigned) {
        return compare_int_uint(a.get<std::int64_t>(), b.get<std::uint64_t>());
    }
    if (ta == vt::number_unsigned && tb == vt::number_integer) {
        return reverse(compare_int_uint(b.get<std::int64_t>(), a.get<std::uint64_t>()));
    }
    if (ta == vt::number_integer && tb == vt::number_float) {
        return compare_int_double(a.get<std::int64_t>(), b.get<double>());
    }
    if (ta == vt::number_float && tb == vt::number_integer) {
        return reverse(compare_int_double(b.get<std::int64_t>(), a.get<double>()));
    }
    if (ta == vt::number_unsigned && tb == vt::number_float) {
        return compare_uint_double(a.get<std::uint64_t>(), b.get<double>());
    }
    if (ta == vt::number_float && tb == vt::number_unsigned) {
        return reverse(compare_uint_double(b.get<std::uint64_t>(), a.get<double>()));
    }
    return std::partial_ordering::unordered;
}

bool equal(const json& a, const json& b) noexcept
{
    const kind ka = kind_of(a);
    if (ka != kind_of(b)) {
        return false;
    }
    switch (ka) {
    case kind::null:
        return true;
    case kind::boolean:
        return a.get<bool>() == b.get<bool>();
    case kind::number:
        return compare_numbers(a, b) == std::partial_ordering::equivalent;
    case kind::string:
        return string_view_of(a) == string_view_of(b);
    case kind::array: {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!equal(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    case kind::object: {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key, value] : a.items()) {
            const auto it = b.find(key);
            if (it == b.end() || !equal(value, *it)) {
                return false;
            }
        }
        return true;
    }
    case kind::unsupported:
        return a == b;
    }
    return false;
}

std::size_t hash(const json& value) noexcept
{
    switch (value.type()) {
    case json::value_t::null:
        return tag_null;
    case json::value_t::boolean:
        return value.get<bool>() ? tag_true : tag_false;
    case json::value_t::number_integer: {
        const auto i = value.get<std::int64_t>();
        std::size_t seed = i < 0 ? tag_negative_integer : tag_nonnegative_integer;
        hash_combine(seed, i < 0 ? std::hash<std::int64_t>{}(i) : std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(i)));
        return seed;
    }
    case json::value_t::number_unsigned: {
        std::size_t seed = tag_nonnegative_integer;
        hash_combine(seed, std::hash<std::uint64_t>{}(value.get<std::uint64_t>()));
        return seed;
    }
    case json::value_t::number_float:
        return hash_double(value.get<double>());
    case json::value_t::string: {
        std::size_t seed = tag_string;
        hash_combine(seed, std::hash<std::string_view>{}(string_view_of(value)));
        return seed;
    }
    case json::value_t::array: {
        std::size_t seed = tag_array;
        for (const auto& element : value) {
            hash_combine(seed, hash(element));
        }
        return seed;
    }
    case json::value_t::object: {
        // Order-independent: sum of per-member hashes.
        std::size_t sum = 0;
        for (const auto& [key, member] : value.items()) {
            std::size_t member_seed = std::hash<std::string_view>{}(key);
            hash_combine(member_seed, hash(member));
            sum += member_seed;
        }
        std::size_t seed = tag_object;
        hash_combine(seed, sum);
        return seed;
    }
    case json::value_t::binary:
    case json::value_t::discarded:
        return tag_unsupported;
    }
    return tag_unsupported;
}

std::size_t code_point_count(std::string_view utf8) noexcept
{
    std::size_t count = 0;
    for (const char c : utf8) {
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

} // namespace jsonschema::detail::json_access
