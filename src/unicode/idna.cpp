// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include "unicode/idna.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "unicode/unicode.hpp"

namespace jsonschema::detail::idna {

namespace {

using unicode::bidi_class;

// --- Punycode (RFC 3492) --------------------------------------------------------------

constexpr std::uint32_t base = 36;
constexpr std::uint32_t tmin = 1;
constexpr std::uint32_t tmax = 26;
constexpr std::uint32_t skew = 38;
constexpr std::uint32_t damp = 700;
constexpr std::uint32_t initial_bias = 72;
constexpr std::uint32_t initial_n = 128;
constexpr std::uint32_t maxint = std::numeric_limits<std::uint32_t>::max();

std::uint32_t adapt(std::uint32_t delta, std::uint32_t points, bool first) noexcept
{
    delta = first ? delta / damp : delta / 2;
    delta += delta / points;
    std::uint32_t k = 0;
    while (delta > ((base - tmin) * tmax) / 2) {
        delta /= base - tmin;
        k += base;
    }
    return k + (base - tmin + 1) * delta / (delta + skew);
}

constexpr std::uint32_t threshold(std::uint32_t k, std::uint32_t bias) noexcept
{
    return k <= bias ? tmin : k >= bias + tmax ? tmax : k - bias;
}

constexpr std::uint32_t decode_digit(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint32_t>(c - '0') + 26;
    }
    if (c >= 'a' && c <= 'z') {
        return static_cast<std::uint32_t>(c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return static_cast<std::uint32_t>(c - 'A');
    }
    return base;
}

constexpr char encode_digit(std::uint32_t d) noexcept
{
    return static_cast<char>(d < 26 ? 'a' + d : '0' + (d - 26));
}

// --- label validity -------------------------------------------------------------------

bool is_ascii(std::u32string_view s) noexcept
{
    return std::all_of(s.begin(), s.end(), [](char32_t c) { return c < 0x80; });
}

bool starts_with_ace_prefix(std::u32string_view label) noexcept
{
    return label.size() >= 4 && label[0] == U'x' && label[1] == U'n' && label[2] == U'-' && label[3] == U'-';
}

bool is_virama(char32_t cp) noexcept
{
    return unicode::combining_class(cp) == 9;
}

/// RFC 5892 Appendix A.1 and A.2.
bool contextj_holds(std::u32string_view label, std::size_t i) noexcept
{
    if (i > 0 && is_virama(label[i - 1])) {
        return true;
    }
    if (label[i] != U'‌') {
        return false;   // ZERO WIDTH JOINER needs the virama
    }
    // ZERO WIDTH NON-JOINER: (Joining_Type:{L,D})(Joining_Type:T)* ZWNJ (Joining_Type:T)*(Joining_Type:{R,D})
    using unicode::joining_type;
    std::size_t before = i;
    while (before > 0 && unicode::joining(label[before - 1]) == joining_type::T) {
        --before;
    }
    if (before == 0) {
        return false;
    }
    const joining_type left = unicode::joining(label[before - 1]);
    if (left != joining_type::L && left != joining_type::D) {
        return false;
    }
    std::size_t after = i + 1;
    while (after < label.size() && unicode::joining(label[after]) == joining_type::T) {
        ++after;
    }
    if (after == label.size()) {
        return false;
    }
    const joining_type right = unicode::joining(label[after]);
    return right == joining_type::R || right == joining_type::D;
}

/// RFC 5892 Appendix A.3 to A.9.
bool contexto_holds(std::u32string_view label, std::size_t i) noexcept
{
    using unicode::script;
    const char32_t cp = label[i];
    if (cp == U'·') {      // MIDDLE DOT: between two 'l's
        return i > 0 && i + 1 < label.size() && label[i - 1] == U'l' && label[i + 1] == U'l';
    }
    if (cp == U'͵') {      // GREEK LOWER NUMERAL SIGN (KERAIA): Greek follows
        return i + 1 < label.size() && unicode::script_of(label[i + 1]) == script::greek;
    }
    if (cp == U'׳' || cp == U'״') {   // HEBREW GERESH, GERSHAYIM: Hebrew precedes
        return i > 0 && unicode::script_of(label[i - 1]) == script::hebrew;
    }
    if (cp == U'・') {      // KATAKANA MIDDLE DOT: some Hiragana, Katakana or Han in the label
        return std::any_of(label.begin(), label.end(), [](char32_t c) {
            const script s = unicode::script_of(c);
            return s == script::hiragana || s == script::katakana || s == script::han;
        });
    }
    if (cp >= U'٠' && cp <= U'٩') {   // ARABIC-INDIC DIGITS: no Extended Arabic-Indic digits
        return std::none_of(label.begin(), label.end(), [](char32_t c) { return c >= U'۰' && c <= U'۹'; });
    }
    if (cp >= U'۰' && cp <= U'۹') {   // EXTENDED ARABIC-INDIC DIGITS: no Arabic-Indic digits
        return std::none_of(label.begin(), label.end(), [](char32_t c) { return c >= U'٠' && c <= U'٩'; });
    }
    return false;
}

/// RFC 5891 §5.4 (with RFC 5892) for a U-label, or an all-ASCII label, as code points:
/// NFC, hyphen placement, no leading combining mark, and every code point PVALID or
/// CONTEXTJ/CONTEXTO with its rule satisfied. The Bidi rule is checked per domain.
bool is_valid_label(std::u32string_view label)
{
    if (label.empty() || label.front() == U'-' || label.back() == U'-') {
        return false;
    }
    if (label.size() >= 4 && label[2] == U'-' && label[3] == U'-') {
        return false;
    }
    if (unicode::is_mark(label.front())) {
        return false;
    }
    for (std::size_t i = 0; i < label.size(); ++i) {
        switch (unicode::idna2008_property(label[i])) {
        case unicode::idna_property::pvalid:
            break;
        case unicode::idna_property::contextj:
            if (!contextj_holds(label, i)) {
                return false;
            }
            break;
        case unicode::idna_property::contexto:
            if (!contexto_holds(label, i)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return is_ascii(label) || unicode::nfc(label) == label;
}

bool is_rtl_label(std::u32string_view label) noexcept
{
    return std::any_of(label.begin(), label.end(), [](char32_t c) {
        const bidi_class b = unicode::bidi(c);
        return b == bidi_class::R || b == bidi_class::AL || b == bidi_class::AN;
    });
}

/// RFC 5893 §2, for one label of a Bidi domain name.
bool satisfies_bidi_rule(std::u32string_view label) noexcept
{
    const bidi_class first = unicode::bidi(label.front());
    if (first != bidi_class::L && first != bidi_class::R && first != bidi_class::AL) {
        return false;                                                           // rule 1
    }
    const bool rtl = first != bidi_class::L;
    bool has_en = false;
    bool has_an = false;
    bidi_class last = first;
    for (const char32_t c : label) {
        const bidi_class b = unicode::bidi(c);
        switch (b) {
        case bidi_class::EN:
            has_en = true;
            break;
        case bidi_class::AN:
            has_an = true;
            if (!rtl) {
                return false;                                                   // rule 5
            }
            break;
        case bidi_class::R:
        case bidi_class::AL:
            if (!rtl) {
                return false;                                                   // rule 5
            }
            break;
        case bidi_class::L:
            if (rtl) {
                return false;                                                   // rule 2
            }
            break;
        case bidi_class::ES:
        case bidi_class::CS:
        case bidi_class::ET:
        case bidi_class::ON:
        case bidi_class::BN:
        case bidi_class::NSM:
            break;
        default:
            return false;                                                       // rules 2, 5
        }
        if (b != bidi_class::NSM) {
            last = b;
        }
    }
    if (rtl) {
        return (last == bidi_class::R || last == bidi_class::AL || last == bidi_class::EN || last == bidi_class::AN)
            && !(has_en && has_an);                                             // rules 3, 4
    }
    return last == bidi_class::L || last == bidi_class::EN;                     // rule 6
}

/// RFC 5893: the rule applies to every label once any label is right-to-left.
bool satisfies_bidi_rule(const std::vector<std::u32string>& labels) noexcept
{
    if (std::none_of(labels.begin(), labels.end(), [](const std::u32string& l) { return is_rtl_label(l); })) {
        return true;
    }
    return std::all_of(labels.begin(), labels.end(), [](const std::u32string& l) { return satisfies_bidi_rule(l); });
}

/// Decodes the lower-case A-label @p a_label ("xn--..."): its Punycode must be valid,
/// canonical and decode to something non-ASCII. The result still needs is_valid_label.
std::optional<std::u32string> decode_a_label(std::string_view a_label)
{
    const std::string_view encoded = a_label.substr(4);
    auto decoded = punycode_decode(encoded);
    if (!decoded || decoded->empty() || is_ascii(*decoded)) {
        return std::nullopt;
    }
    const auto reencoded = punycode_encode(*decoded);
    if (!reencoded || *reencoded != encoded) {
        return std::nullopt;
    }
    return decoded;
}

std::vector<std::u32string> split_labels(std::u32string_view s)
{
    std::vector<std::u32string> labels;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = s.find(U'.', start);
        labels.emplace_back(s.substr(start, dot == std::u32string_view::npos ? std::u32string_view::npos : dot - start));
        if (dot == std::u32string_view::npos) {
            return labels;
        }
        start = dot + 1;
    }
}

} // namespace

std::optional<std::u32string> punycode_decode(std::string_view encoded)
{
    std::u32string out;
    std::size_t in = 0;
    if (const std::size_t delimiter = encoded.rfind('-'); delimiter != std::string_view::npos && delimiter > 0) {
        for (std::size_t j = 0; j < delimiter; ++j) {
            if (static_cast<unsigned char>(encoded[j]) >= 0x80) {
                return std::nullopt;
            }
            out += static_cast<char32_t>(encoded[j]);
        }
        in = delimiter + 1;
    }
    std::uint32_t n = initial_n;
    std::uint32_t i = 0;
    std::uint32_t bias = initial_bias;
    while (in < encoded.size()) {
        const std::uint32_t old_i = i;
        std::uint32_t w = 1;
        for (std::uint32_t k = base;; k += base) {
            if (in >= encoded.size()) {
                return std::nullopt;
            }
            const std::uint32_t digit = decode_digit(encoded[in++]);
            if (digit >= base || digit > (maxint - i) / w) {
                return std::nullopt;
            }
            i += digit * w;
            const std::uint32_t t = threshold(k, bias);
            if (digit < t) {
                break;
            }
            if (w > maxint / (base - t)) {
                return std::nullopt;
            }
            w *= base - t;
        }
        const auto points = static_cast<std::uint32_t>(out.size() + 1);
        bias = adapt(i - old_i, points, old_i == 0);
        if (i / points > maxint - n) {
            return std::nullopt;
        }
        n += i / points;
        i %= points;
        if (n < 0x80 || n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF)) {
            return std::nullopt;    // basic code points are never encoded
        }
        out.insert(out.begin() + i, static_cast<char32_t>(n));
        ++i;
    }
    return out;
}

std::optional<std::string> punycode_encode(std::u32string_view label)
{
    std::string out;
    for (const char32_t c : label) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        }
    }
    const auto basic = static_cast<std::uint32_t>(out.size());
    std::uint32_t handled = basic;
    if (basic > 0) {
        out += '-';
    }
    std::uint32_t n = initial_n;
    std::uint32_t delta = 0;
    std::uint32_t bias = initial_bias;
    while (handled < label.size()) {
        std::uint32_t m = maxint;
        for (const char32_t c : label) {
            if (c >= n && c < m) {
                m = c;
            }
        }
        if (m - n > (maxint - delta) / (handled + 1)) {
            return std::nullopt;
        }
        delta += (m - n) * (handled + 1);
        n = m;
        for (const char32_t c : label) {
            if (c < n && ++delta == 0) {
                return std::nullopt;
            }
            if (c == n) {
                std::uint32_t q = delta;
                for (std::uint32_t k = base;; k += base) {
                    const std::uint32_t t = threshold(k, bias);
                    if (q < t) {
                        break;
                    }
                    out += encode_digit(t + (q - t) % (base - t));
                    q = (q - t) / (base - t);
                }
                out += encode_digit(q);
                bias = adapt(delta, handled + 1, handled == basic);
                delta = 0;
                ++handled;
            }
        }
        ++delta;
        ++n;
    }
    return out;
}

bool is_idn_hostname(std::string_view utf8)
{
    const auto input = unicode::decode_utf8(utf8);
    if (!input) {
        return false;
    }
    // UTS #46 §4 steps 1-2: map, then normalize.
    std::u32string mapped;
    mapped.reserve(input->size());
    for (const char32_t cp : *input) {
        switch (unicode::uts46_map(cp, mapped)) {
        case unicode::uts46_status::valid:
            mapped += cp;
            break;
        case unicode::uts46_status::mapped:
        case unicode::uts46_status::ignored:
            break;
        case unicode::uts46_status::disallowed:
            return false;
        }
    }
    // Steps 3-4: break into labels; convert A-labels and validate.
    std::vector<std::u32string> labels = split_labels(unicode::nfc(mapped));
    std::size_t length = 0;
    std::u32string scratch;
    for (auto& label : labels) {
        std::size_t a_label_length;
        if (starts_with_ace_prefix(label)) {
            if (!is_ascii(label)) {
                return false;
            }
            std::string ascii;
            for (const char32_t c : label) {
                ascii += static_cast<char>(c);
            }
            auto decoded = decode_a_label(ascii);
            if (!decoded) {
                return false;
            }
            // A decoded label is not mapped again, so it must already be UTS #46 valid.
            for (const char32_t cp : *decoded) {
                if (unicode::uts46_map(cp, scratch) != unicode::uts46_status::valid) {
                    return false;
                }
            }
            label = std::move(*decoded);
            a_label_length = ascii.size();
        } else if (is_ascii(label)) {
            a_label_length = label.size();
        } else {
            const auto encoded = punycode_encode(label);
            if (!encoded) {
                return false;
            }
            a_label_length = 4 + encoded->size();
        }
        if (!is_valid_label(label) || a_label_length > 63) {
            return false;
        }
        length += a_label_length + 1;
    }
    // VerifyDnsLength: at most 253 octets, not counting a root dot (which is rejected).
    return length - 1 <= 253 && satisfies_bidi_rule(labels);
}

bool has_valid_a_labels(std::string_view ascii_hostname)
{
    std::vector<std::u32string> labels;
    bool any_a_label = false;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = ascii_hostname.find('.', start);
        const std::string_view label = ascii_hostname.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        std::string lower(label);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        if (lower.size() >= 4 && lower.compare(0, 4, "xn--") == 0) {
            auto decoded = decode_a_label(lower);
            if (!decoded || !is_valid_label(*decoded)) {
                return false;
            }
            labels.push_back(std::move(*decoded));
            any_a_label = true;
        } else {
            labels.emplace_back(lower.begin(), lower.end());
        }
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return !any_a_label || satisfies_bidi_rule(labels);
}

} // namespace jsonschema::detail::idna
