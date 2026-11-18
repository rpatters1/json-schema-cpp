// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "formats.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <jsonschema/uri.hpp>

#include "json/pointer.hpp"
#include "regex.hpp"
#include "unicode/idna.hpp"
#include "unicode/unicode.hpp"

namespace jsonschema::detail {

namespace {

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_alpha(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
constexpr bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

/// Reads exactly @p count ASCII digits at @p pos.
std::optional<int> digits(std::string_view s, std::size_t& pos, std::size_t count) noexcept
{
    if (pos + count > s.size()) {
        return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = s[pos + i];
        if (!is_digit(c)) {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    pos += count;
    return value;
}

bool expect(std::string_view s, std::size_t& pos, char c) noexcept
{
    if (pos < s.size() && s[pos] == c) {
        ++pos;
        return true;
    }
    return false;
}

constexpr bool is_leap_year(int y) noexcept { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/// RFC 3339 full-date at @p pos.
bool parse_date(std::string_view s, std::size_t& pos) noexcept
{
    const auto year = digits(s, pos, 4);
    if (!year || !expect(s, pos, '-')) {
        return false;
    }
    const auto month = digits(s, pos, 2);
    if (!month || *month < 1 || *month > 12 || !expect(s, pos, '-')) {
        return false;
    }
    const auto day = digits(s, pos, 2);
    static constexpr std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int last = *month == 2 && is_leap_year(*year) ? 29 : days[static_cast<std::size_t>(*month - 1)];
    return day && *day >= 1 && *day <= last;
}

/// RFC 3339 full-time at @p pos: partial-time and a mandatory time-offset. A leap second
/// (:60) is valid only at 23:59 UTC, after applying the offset.
bool parse_time(std::string_view s, std::size_t& pos) noexcept
{
    const auto hour = digits(s, pos, 2);
    if (!hour || *hour > 23 || !expect(s, pos, ':')) {
        return false;
    }
    const auto minute = digits(s, pos, 2);
    if (!minute || *minute > 59 || !expect(s, pos, ':')) {
        return false;
    }
    const auto second = digits(s, pos, 2);
    if (!second || *second > 60) {
        return false;
    }
    if (expect(s, pos, '.')) {
        const std::size_t start = pos;
        while (pos < s.size() && is_digit(s[pos])) {
            ++pos;
        }
        if (pos == start) {
            return false;
        }
    }
    if (pos >= s.size()) {
        return false;   // the offset is required
    }
    int offset = 0;     // minutes east of UTC
    if (s[pos] == 'Z' || s[pos] == 'z') {
        ++pos;
    } else if (s[pos] == '+' || s[pos] == '-') {
        const int sign = s[pos] == '+' ? 1 : -1;
        ++pos;
        const auto oh = digits(s, pos, 2);
        if (!oh || *oh > 23 || !expect(s, pos, ':')) {
            return false;
        }
        const auto om = digits(s, pos, 2);
        if (!om || *om > 59) {
            return false;
        }
        offset = sign * (*oh * 60 + *om);
    } else {
        return false;
    }
    if (*second == 60) {
        const int utc = ((*hour * 60 + *minute - offset) % 1440 + 1440) % 1440;
        if (utc != 23 * 60 + 59) {
            return false;
        }
    }
    return true;
}

// --- email (RFC 5321 Mailbox) ---------------------------------------------------------

constexpr bool is_atext(char c) noexcept
{
    return is_alpha(c) || is_digit(c) || std::string_view("!#$%&'*+-/=?^_`{|}~").find(c) != std::string_view::npos;
}

/// RFC 5321 Dot-string; with @p utf8, RFC 6531 also allows any non-ASCII character in atoms.
bool is_dot_string(std::string_view s, bool utf8) noexcept
{
    if (s.empty() || s.front() == '.' || s.back() == '.') {
        return false;
    }
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            std::size_t end = i;
            if (!utf8 || !unicode::decode_utf8(s, end)) {
                return false;
            }
            i = end;
            continue;
        }
        if (c == '.') {
            if (s[i + 1] == '.') {
                return false;
            }
        } else if (!is_atext(static_cast<char>(c))) {
            return false;
        }
        ++i;
    }
    return true;
}

/// RFC 5321 Quoted-string; with @p utf8, RFC 6531 also allows any non-ASCII character.
bool is_quoted_string(std::string_view s, bool utf8) noexcept
{
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') {
        return false;
    }
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            std::size_t end = i;
            if (!utf8 || !unicode::decode_utf8(s, end) || end > s.size() - 1) {
                return false;
            }
            i = end - 1;
        } else if (c == '\\') {
            if (i + 2 >= s.size()) {
                return false;
            }
            const auto next = static_cast<unsigned char>(s[++i]);
            if (next < 32 || next > 126) {
                return false;
            }
        } else if (!(c == 32 || c == 33 || (c >= 35 && c <= 91) || (c >= 93 && c <= 126))) {
            return false;
        }
    }
    return true;
}

/// RFC 5321 Domain: sub-domains of letters, digits and hyphens.
bool is_smtp_domain(std::string_view s) noexcept
{
    if (s.empty() || s.size() > 255) {
        return false;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = s.find('.', start);
        const std::string_view label = s.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') {
            return false;
        }
        for (const char c : label) {
            if (!is_alpha(c) && !is_digit(c) && c != '-') {
                return false;
            }
        }
        if (dot == std::string_view::npos) {
            return true;
        }
        start = dot + 1;
    }
}

bool is_address_literal(std::string_view s)
{
    if (s.size() < 3 || s.front() != '[' || s.back() != ']') {
        return false;
    }
    const std::string_view inner = s.substr(1, s.size() - 2);
    if (is_ipv4_address(inner)) {
        return true;
    }
    if (inner.size() > 5 && (inner.substr(0, 5) == "IPv6:" || inner.substr(0, 5) == "ipv6:")) {
        return is_ipv6_address(inner.substr(5));
    }
    // General-address-literal: Standardized-tag ":" 1*dcontent
    const std::size_t colon = inner.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == inner.size()) {
        return false;
    }
    const std::string_view tag = inner.substr(0, colon);
    if (tag.front() == '-' || tag.back() == '-') {
        return false;
    }
    for (const char c : tag) {
        if (!is_alpha(c) && !is_digit(c) && c != '-') {
            return false;
        }
    }
    for (const char c : inner.substr(colon + 1)) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 33 || u > 126 || u == '[' || u == '\\' || u == ']') {
            return false;
        }
    }
    return true;
}

/// RFC 5321 Mailbox; with @p utf8, the RFC 6531 extension: non-ASCII local parts, and a
/// domain that is an idn-hostname.
bool is_mailbox(std::string_view s, bool utf8) noexcept
{
    const std::size_t at = s.rfind('@');
    if (at == std::string_view::npos || at == 0 || at + 1 == s.size()) {
        return false;
    }
    const std::string_view local = s.substr(0, at);
    const std::string_view domain = s.substr(at + 1);
    if (local.size() > 64) {
        return false;
    }
    if (!is_dot_string(local, utf8) && !is_quoted_string(local, utf8)) {
        return false;
    }
    try {
        if (domain.front() == '[') {
            return is_address_literal(domain);
        }
        return utf8 ? idna::is_idn_hostname(domain) : is_smtp_domain(domain);
    } catch (...) {
        return false;
    }
}

// --- IRI (RFC 3987) -------------------------------------------------------------------

/// RFC 3987 ucschar.
bool is_ucschar(std::uint32_t cp) noexcept
{
    if ((cp >= 0xA0 && cp <= 0xD7FF) || (cp >= 0xF900 && cp <= 0xFDCF) || (cp >= 0xFDF0 && cp <= 0xFFEF)) {
        return true;
    }
    if (cp >= 0x10000 && cp <= 0xEFFFD) {
        return (cp & 0xFFFF) <= 0xFFFD;     // excludes the noncharacters at the end of each plane
    }
    return false;
}

/// RFC 3987 iprivate (allowed in the query only).
bool is_iprivate(std::uint32_t cp) noexcept
{
    return (cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0xFFFFD) || (cp >= 0x100000 && cp <= 0x10FFFD);
}

/// Maps an IRI to a URI by percent-encoding the allowed non-ASCII characters
/// (RFC 3987 §3.1), or nullopt if a character is not allowed where it appears.
std::optional<std::string> iri_to_uri(std::string_view s)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    bool in_query = false;
    bool in_fragment = false;
    for (std::size_t pos = 0; pos < s.size();) {
        const std::size_t start = pos;
        const auto cp = unicode::decode_utf8(s, pos);
        if (!cp) {
            return std::nullopt;
        }
        if (*cp < 0x80) {
            const char c = static_cast<char>(*cp);
            if (c == '?' && !in_fragment) {
                in_query = true;
            } else if (c == '#') {
                in_query = false;
                in_fragment = true;
            }
            out += c;
            continue;
        }
        if (!is_ucschar(*cp) && !(in_query && is_iprivate(*cp))) {
            return std::nullopt;
        }
        for (std::size_t i = start; i < pos; ++i) {
            const auto byte = static_cast<unsigned char>(s[i]);
            out += '%';
            out += hex[byte >> 4];
            out += hex[byte & 0xF];
        }
    }
    return out;
}

// --- formats --------------------------------------------------------------------------

bool format_date(std::string_view s) { return is_date(s); }
bool format_time(std::string_view s) { return is_time(s); }
bool format_date_time(std::string_view s) { return is_date_time(s); }
bool format_duration(std::string_view s) { return is_duration(s); }
bool format_email(std::string_view s) { return is_email(s); }
bool format_hostname(std::string_view s) { return is_hostname(s); }
bool format_idn_email(std::string_view s) { return is_idn_email(s); }
bool format_idn_hostname(std::string_view s) { return idna::is_idn_hostname(s); }
bool format_ipv4(std::string_view s) { return is_ipv4_address(s); }
bool format_ipv6(std::string_view s) { return is_ipv6_address(s); }
bool format_uri(std::string_view s)
{
    const auto u = uri::parse(s);
    return u && u->has_scheme();
}
bool format_uri_reference(std::string_view s) { return uri::parse(s).has_value(); }
bool format_iri(std::string_view s) { return is_iri(s); }
bool format_iri_reference(std::string_view s) { return is_iri_reference(s); }
bool format_uuid(std::string_view s) { return is_uuid(s); }
bool format_uri_template(std::string_view s) { return is_uri_template(s); }
bool format_json_pointer(std::string_view s) { return json_pointer::parse(s).has_value(); }
bool format_relative_json_pointer(std::string_view s) { return is_relative_json_pointer(s); }
bool format_regex(std::string_view s) { return compile_regex(s) != nullptr; }

} // namespace

bool is_date(std::string_view s) noexcept
{
    std::size_t pos = 0;
    return parse_date(s, pos) && pos == s.size();
}

bool is_time(std::string_view s) noexcept
{
    std::size_t pos = 0;
    return parse_time(s, pos) && pos == s.size();
}

bool is_date_time(std::string_view s) noexcept
{
    std::size_t pos = 0;
    if (!parse_date(s, pos) || pos >= s.size() || (s[pos] != 'T' && s[pos] != 't')) {
        return false;
    }
    ++pos;
    return parse_time(s, pos) && pos == s.size();
}

bool is_duration(std::string_view s) noexcept
{
    // RFC 3339 Appendix A: "P" (dur-date [dur-time] / dur-time / dur-week). The grammar nests
    // the components (dur-year = 1*DIGIT "Y" [dur-month], dur-month = 1*DIGIT "M" [dur-day],
    // ...), so after the first one each must be the next in sequence: "P1Y2D" is invalid.
    std::size_t pos = 0;
    if (!expect(s, pos, 'P') || pos == s.size()) {
        return false;
    }
    const auto component = [&](std::string_view allowed, std::size_t& rank) -> int {
        // Returns 1 for a component, 0 at the end or before 'T', -1 if invalid.
        if (pos == s.size() || s[pos] == 'T') {
            return 0;
        }
        const std::size_t start = pos;
        while (pos < s.size() && is_digit(s[pos])) {
            ++pos;
        }
        if (pos == start || pos == s.size()) {
            return -1;
        }
        const std::size_t at = allowed.find(s[pos]);
        if (at == std::string_view::npos || (rank != 0 && at != rank)) {
            return -1;
        }
        rank = at + 1;
        ++pos;
        return 1;
    };
    // dur-week stands alone.
    const std::size_t w = s.find('W');
    if (w != std::string_view::npos) {
        if (w + 1 != s.size() || w == 1) {
            return false;
        }
        for (std::size_t i = 1; i < w; ++i) {
            if (!is_digit(s[i])) {
                return false;
            }
        }
        return true;
    }
    std::size_t rank = 0;
    int date_components = 0;
    for (int r; (r = component("YMD", rank)) != 0;) {
        if (r < 0) {
            return false;
        }
        ++date_components;
    }
    if (pos == s.size()) {
        return date_components > 0;
    }
    ++pos;  // 'T'
    rank = 0;
    int time_components = 0;
    for (int r; (r = component("HMS", rank)) != 0;) {
        if (r < 0) {
            return false;
        }
        ++time_components;
    }
    return time_components > 0 && pos == s.size();
}

bool is_email(std::string_view s) noexcept
{
    return is_mailbox(s, false);
}

bool is_idn_email(std::string_view s)
{
    return is_mailbox(s, true);
}

bool is_hostname(std::string_view s)
{
    // RFC 1123 host names: dot-separated labels of letters, digits and hyphens, each 1-63
    // characters, not starting or ending with a hyphen; at most 253 characters in all.
    // A label starting "xn--" (in any case) must be a valid IDNA2008 A-label; other labels
    // with "--" in positions 3-4 are allowed, since RFC 1123 does not reserve them.
    if (s.empty() || s.size() > 253) {
        return false;
    }
    std::size_t start = 0;
    bool a_labels = false;
    while (true) {
        const std::size_t dot = s.find('.', start);
        const std::string_view label = s.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') {
            return false;
        }
        for (const char c : label) {
            if (!is_alpha(c) && !is_digit(c) && c != '-') {
                return false;
            }
        }
        a_labels = a_labels
            || (label.size() >= 4 && (label[0] | 0x20) == 'x' && (label[1] | 0x20) == 'n' && label[2] == '-' && label[3] == '-');
        if (dot == std::string_view::npos) {
            return !a_labels || idna::has_valid_a_labels(s);
        }
        start = dot + 1;
    }
}

bool is_iri(std::string_view s)
{
    const auto mapped = iri_to_uri(s);
    if (!mapped) {
        return false;
    }
    const auto u = uri::parse(*mapped);
    return u && u->has_scheme();
}

bool is_iri_reference(std::string_view s)
{
    const auto mapped = iri_to_uri(s);
    return mapped && uri::parse(*mapped).has_value();
}

bool is_uuid(std::string_view s) noexcept
{
    // RFC 9562: 8-4-4-4-12 hexadecimal digits, any case.
    if (s.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !is_hex(s[i])) {
            return false;
        }
    }
    return true;
}

bool is_uri_template(std::string_view s) noexcept
{
    // RFC 6570 §2.
    for (std::size_t pos = 0; pos < s.size();) {
        const auto c = static_cast<unsigned char>(s[pos]);
        if (c == '{') {
            const std::size_t end = s.find('}', pos);
            if (end == std::string_view::npos) {
                return false;
            }
            std::string_view expression = s.substr(pos + 1, end - pos - 1);
            if (!expression.empty() && std::string_view("+#./;?&=,!@|").find(expression.front()) != std::string_view::npos) {
                expression.remove_prefix(1);
            }
            if (expression.empty()) {
                return false;
            }
            // variable-list = varspec *( "," varspec ); varspec = varname [ modifier-level4 ]
            std::size_t i = 0;
            while (true) {
                const std::size_t name_start = i;
                bool previous_dot = true;   // no leading "."
                while (i < expression.size()) {
                    const char v = expression[i];
                    if (is_alpha(v) || is_digit(v) || v == '_') {
                        previous_dot = false;
                        ++i;
                    } else if (v == '%' && i + 2 < expression.size() && is_hex(expression[i + 1]) && is_hex(expression[i + 2])) {
                        previous_dot = false;
                        i += 3;
                    } else if (v == '.' && !previous_dot) {
                        previous_dot = true;
                        ++i;
                    } else {
                        break;
                    }
                }
                if (i == name_start || previous_dot) {
                    return false;
                }
                if (i < expression.size() && expression[i] == ':') {
                    const std::size_t digits_start = ++i;
                    while (i < expression.size() && is_digit(expression[i])) {
                        ++i;
                    }
                    const std::size_t count = i - digits_start;
                    if (count == 0 || count > 4 || expression[digits_start] == '0') {
                        return false;
                    }
                } else if (i < expression.size() && expression[i] == '*') {
                    ++i;
                }
                if (i == expression.size()) {
                    break;
                }
                if (expression[i] != ',') {
                    return false;
                }
                ++i;
            }
            pos = end + 1;
            continue;
        }
        if (c == '%') {
            if (pos + 2 >= s.size() || !is_hex(s[pos + 1]) || !is_hex(s[pos + 2])) {
                return false;
            }
            pos += 3;
            continue;
        }
        // literals: any character except CTL, SP, DQUOTE, "%", "<", ">", "\", "^", "`", "{", "|", "}".
        // (RFC 6570's ABNF also leaves out "'", but the test suite follows the prose, which allows it.)
        if (c <= 0x20 || c == 0x7F || std::string_view("\"<>\\^`|}").find(static_cast<char>(c)) != std::string_view::npos) {
            return false;
        }
        ++pos;
    }
    return true;
}

bool is_relative_json_pointer(std::string_view s) noexcept
{
    // draft-handrews-relative-json-pointer-01: non-negative-integer ( "#" / json-pointer )
    std::size_t pos = 0;
    while (pos < s.size() && is_digit(s[pos])) {
        ++pos;
    }
    if (pos == 0 || (pos > 1 && s[0] == '0')) {
        return false;
    }
    const std::string_view rest = s.substr(pos);
    return rest == "#" || json_pointer::parse(rest).has_value();
}

format_function find_builtin_format(std::string_view name) noexcept
{
    static constexpr std::array<std::pair<std::string_view, format_function>, 19> table = {{
        {"date", format_date},
        {"date-time", format_date_time},
        {"duration", format_duration},
        {"email", format_email},
        {"hostname", format_hostname},
        {"idn-email", format_idn_email},
        {"idn-hostname", format_idn_hostname},
        {"ipv4", format_ipv4},
        {"ipv6", format_ipv6},
        {"iri", format_iri},
        {"iri-reference", format_iri_reference},
        {"json-pointer", format_json_pointer},
        {"regex", format_regex},
        {"relative-json-pointer", format_relative_json_pointer},
        {"time", format_time},
        {"uri", format_uri},
        {"uri-reference", format_uri_reference},
        {"uri-template", format_uri_template},
        {"uuid", format_uuid},
    }};
    for (const auto& [n, f] : table) {
        if (n == name) {
            return f;
        }
    }
    return nullptr;
}

} // namespace jsonschema::detail
