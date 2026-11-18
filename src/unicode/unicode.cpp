// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "unicode/unicode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <utility>

namespace jsonschema::detail::unicode {

namespace {

#include "unicode/tables.inc"

/// Looks up a run-length table of (first << 8) | value entries.
template <std::size_t N>
std::uint8_t lookup(const std::uint32_t (&table)[N], char32_t cp) noexcept
{
    const std::uint32_t key = (static_cast<std::uint32_t>(cp) << 8) | 0xFFu;
    const auto it = std::upper_bound(std::begin(table), std::end(table), key);
    return static_cast<std::uint8_t>(*(it - 1) & 0xFFu);   // table[0] starts at U+0000
}

// Hangul syllables (Unicode §3.12) compose and decompose algorithmically.
constexpr char32_t hangul_s_base = 0xAC00;
constexpr char32_t hangul_l_base = 0x1100;
constexpr char32_t hangul_v_base = 0x1161;
constexpr char32_t hangul_t_base = 0x11A7;
constexpr char32_t hangul_l_count = 19;
constexpr char32_t hangul_v_count = 21;
constexpr char32_t hangul_t_count = 28;
constexpr char32_t hangul_n_count = hangul_v_count * hangul_t_count;
constexpr char32_t hangul_s_count = hangul_l_count * hangul_n_count;

void decompose(char32_t cp, std::u32string& out)
{
    if (cp >= hangul_s_base && cp < hangul_s_base + hangul_s_count) {
        const char32_t index = cp - hangul_s_base;
        out += hangul_l_base + index / hangul_n_count;
        out += hangul_v_base + (index % hangul_n_count) / hangul_t_count;
        if (const char32_t t = index % hangul_t_count; t != 0) {
            out += hangul_t_base + t;
        }
        return;
    }
    const auto it = std::lower_bound(std::begin(decomposition_table), std::end(decomposition_table), cp,
                                     [](const decomposition_entry& e, char32_t c) { return e.cp < c; });
    if (it == std::end(decomposition_table) || it->cp != cp) {
        out += cp;
        return;
    }
    decompose(it->first, out);
    if (it->second != 0) {
        decompose(it->second, out);
    }
}

/// The primary composite of @p first and @p second, or 0 if there is none.
char32_t compose(char32_t first, char32_t second) noexcept
{
    if (first >= hangul_l_base && first < hangul_l_base + hangul_l_count
        && second >= hangul_v_base && second < hangul_v_base + hangul_v_count) {
        return hangul_s_base + ((first - hangul_l_base) * hangul_v_count + (second - hangul_v_base)) * hangul_t_count;
    }
    if (first >= hangul_s_base && first < hangul_s_base + hangul_s_count && (first - hangul_s_base) % hangul_t_count == 0
        && second > hangul_t_base && second < hangul_t_base + hangul_t_count) {
        return first + (second - hangul_t_base);
    }
    const auto it = std::lower_bound(std::begin(composition_table), std::end(composition_table), std::pair{first, second},
                                     [](const composition_entry& e, const std::pair<char32_t, char32_t>& key) {
                                         return e.first != key.first ? e.first < key.first : e.second < key.second;
                                     });
    if (it != std::end(composition_table) && it->first == first && it->second == second) {
        return it->composite;
    }
    return 0;
}

} // namespace

std::string_view version() noexcept
{
    return unicode_version;
}

std::optional<char32_t> decode_utf8(std::string_view s, std::size_t& pos) noexcept
{
    const auto lead = static_cast<unsigned char>(s[pos]);
    std::size_t length;
    char32_t cp;
    if (lead < 0x80) {
        ++pos;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        cp = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        cp = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        cp = lead & 0x07u;
    } else {
        return std::nullopt;
    }
    if (pos + length > s.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto c = static_cast<unsigned char>(s[pos + i]);
        if ((c & 0xC0) != 0x80) {
            return std::nullopt;
        }
        cp = (cp << 6) | (c & 0x3Fu);
    }
    static constexpr std::array<char32_t, 5> minimum = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < minimum[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return std::nullopt;
    }
    pos += length;
    return cp;
}

std::optional<std::u32string> decode_utf8(std::string_view s)
{
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t pos = 0; pos < s.size();) {
        const auto cp = decode_utf8(s, pos);
        if (!cp) {
            return std::nullopt;
        }
        out += *cp;
    }
    return out;
}

void append_utf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

idna_property idna2008_property(char32_t cp) noexcept
{
    return cp > 0x10FFFF ? idna_property::disallowed : static_cast<idna_property>(lookup(idna2008_table, cp));
}

bidi_class bidi(char32_t cp) noexcept
{
    return cp > 0x10FFFF ? bidi_class::L : static_cast<bidi_class>(lookup(bidi_table, cp));
}

joining_type joining(char32_t cp) noexcept
{
    return cp > 0x10FFFF ? joining_type::U : static_cast<joining_type>(lookup(joining_table, cp));
}

script script_of(char32_t cp) noexcept
{
    return cp > 0x10FFFF ? script::other : static_cast<script>(lookup(script_table, cp));
}

bool is_mark(char32_t cp) noexcept
{
    return cp <= 0x10FFFF && lookup(mark_table, cp) != 0;
}

std::uint8_t combining_class(char32_t cp) noexcept
{
    return cp > 0x10FFFF ? 0 : lookup(ccc_table, cp);
}

uts46_status uts46_map(char32_t cp, std::u32string& mapping)
{
    if (cp > 0x10FFFF) {
        return uts46_status::disallowed;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(cp) << 2) | 3u;
    const auto it = std::upper_bound(std::begin(uts46_keys), std::end(uts46_keys), key) - 1;
    const auto status = static_cast<uts46_status>(*it & 3u);
    if (status == uts46_status::mapped) {
        const std::uint32_t packed = uts46_mappings[it - std::begin(uts46_keys)];
        const std::string_view text(reinterpret_cast<const char*>(uts46_pool) + (packed >> 8), packed & 0xFFu);
        for (std::size_t pos = 0; pos < text.size();) {
            mapping += *decode_utf8(text, pos);
        }
    }
    return status;
}

std::u32string nfc(std::u32string_view s)
{
    // Canonical decomposition, then canonical ordering of each run of non-starters.
    std::u32string d;
    d.reserve(s.size());
    for (const char32_t cp : s) {
        decompose(cp, d);
    }
    for (std::size_t i = 1; i < d.size(); ++i) {
        const std::uint8_t cc = combining_class(d[i]);
        if (cc == 0) {
            continue;
        }
        for (std::size_t j = i; j > 0 && combining_class(d[j - 1]) > cc; --j) {
            std::swap(d[j - 1], d[j]);
        }
    }
    // Canonical composition (Unicode §3.11, D117).
    std::u32string out;
    out.reserve(d.size());
    std::size_t starter = std::u32string::npos;
    std::uint8_t last_class = 0;
    for (const char32_t cp : d) {
        const std::uint8_t cc = combining_class(cp);
        if (starter != std::u32string::npos) {
            const bool adjacent = out.size() == starter + 1;
            const bool blocked = !adjacent && (last_class == 0 || last_class >= cc);
            if (!blocked) {
                if (const char32_t composite = compose(out[starter], cp); composite != 0) {
                    out[starter] = composite;
                    continue;
                }
            }
        }
        if (cc == 0) {
            starter = out.size();
        }
        last_class = cc;
        out += cp;
    }
    return out;
}

} // namespace jsonschema::detail::unicode
