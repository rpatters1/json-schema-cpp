// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "content.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace jsonschema::detail {

namespace {

constexpr char to_lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr int base64_value(char c, bool url) noexcept
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == (url ? '-' : '+')) return 62;
    if (c == (url ? '_' : '/')) return 63;
    return -1;
}

/// RFC 4648 §4 and §5. base64url padding is optional (§5 allows omitting it).
std::optional<std::string> decode_64(std::string_view s, bool url)
{
    std::size_t data = s.size();
    while (data > 0 && s[data - 1] == '=') {
        --data;
    }
    const std::size_t padding = s.size() - data;
    if (padding > 2 || (padding > 0 && s.size() % 4 != 0) || (!url && s.size() % 4 != 0) || data % 4 == 1) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(data / 4 * 3 + 2);
    std::uint32_t bits = 0;
    int count = 0;
    for (std::size_t i = 0; i < data; ++i) {
        const int value = base64_value(s[i], url);
        if (value < 0) {
            return std::nullopt;
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out += static_cast<char>((bits >> count) & 0xFFu);
        }
    }
    return out;
}

constexpr int base32_value(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

constexpr int hex_value(char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

bool equal_ignoring_case(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower(a[i]) != to_lower(b[i])) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> decode_base64(std::string_view encoded)
{
    return decode_64(encoded, false);
}

std::optional<std::string> decode_base64url(std::string_view encoded)
{
    return decode_64(encoded, true);
}

std::optional<std::string> decode_base32(std::string_view s)
{
    // RFC 4648 §6: groups of 8 characters, padded with '='; a group ends with 0, 1, 3, 4 or 6 '='.
    if (s.size() % 8 != 0) {
        return std::nullopt;
    }
    std::size_t data = s.size();
    while (data > 0 && s[data - 1] == '=') {
        --data;
    }
    const std::size_t padding = s.size() - data;
    if (padding != 0 && padding != 1 && padding != 3 && padding != 4 && padding != 6) {
        return std::nullopt;
    }
    std::string out;
    std::uint64_t bits = 0;
    int count = 0;
    for (std::size_t i = 0; i < data; ++i) {
        const int value = base32_value(s[i]);
        if (value < 0) {
            return std::nullopt;
        }
        bits = (bits << 5) | static_cast<std::uint64_t>(value);
        count += 5;
        if (count >= 8) {
            count -= 8;
            out += static_cast<char>((bits >> count) & 0xFFu);
        }
    }
    return out;
}

std::optional<std::string> decode_base16(std::string_view s)
{
    if (s.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int high = hex_value(s[i]);
        const int low = hex_value(s[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        out += static_cast<char>((high << 4) | low);
    }
    return out;
}

content_decode_function find_builtin_encoding(std::string_view name) noexcept
{
    static constexpr std::array<std::pair<std::string_view, content_decode_function>, 4> table = {{
        {"base16", decode_base16},
        {"base32", decode_base32},
        {"base64", decode_base64},
        {"base64url", decode_base64url},
    }};
    for (const auto& [n, f] : table) {
        if (equal_ignoring_case(n, name)) {
            return f;
        }
    }
    return nullptr;
}

std::string normalize_media_type(std::string_view media_type)
{
    if (const std::size_t semicolon = media_type.find(';'); semicolon != std::string_view::npos) {
        media_type = media_type.substr(0, semicolon);
    }
    while (!media_type.empty() && (media_type.front() == ' ' || media_type.front() == '\t')) {
        media_type.remove_prefix(1);
    }
    while (!media_type.empty() && (media_type.back() == ' ' || media_type.back() == '\t')) {
        media_type.remove_suffix(1);
    }
    std::string out(media_type);
    for (char& c : out) {
        c = to_lower(c);
    }
    return out;
}

bool is_json_media_type(std::string_view media_type) noexcept
{
    if (media_type == "application/json") {
        return true;
    }
    const std::size_t slash = media_type.find('/');
    return slash != std::string_view::npos && slash > 0 && media_type.size() > slash + 6 && media_type.ends_with("+json");
}

} // namespace jsonschema::detail
