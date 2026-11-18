// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The Unicode support the IDNA checks need (design doc 5.4): UTF-8, the character
// properties used by RFC 5892 and RFC 5893, UTS #46 mapping and NFC. The data comes from
// tables.inc, which tools/unicode/generate_tables.py generates from the UCD.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace jsonschema::detail::unicode {

/// The Unicode version the tables were generated from.
[[nodiscard]] std::string_view version() noexcept;

/// Decodes one UTF-8 code point at @p pos and advances it; nullopt if ill-formed
/// (overlong, surrogate, out of range or truncated). @p pos must be < s.size().
[[nodiscard]] std::optional<char32_t> decode_utf8(std::string_view s, std::size_t& pos) noexcept;

/// Decodes all of @p s; nullopt if it is not well-formed UTF-8.
[[nodiscard]] std::optional<std::u32string> decode_utf8(std::string_view s);

/// Appends @p cp to @p out as UTF-8.
void append_utf8(std::string& out, char32_t cp);

/// RFC 5892 derived property values.
enum class idna_property : std::uint8_t { pvalid, contextj, contexto, disallowed, unassigned };

/// Bidi_Class values (RFC 5893 uses the first ten).
enum class bidi_class : std::uint8_t {
    L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON, LRE, LRO, RLE, RLO, PDF, LRI, RLI, FSI, PDI
};

/// Joining_Type values.
enum class joining_type : std::uint8_t { U, C, D, L, R, T };

/// The scripts the RFC 5892 contextual rules name; everything else is `other`.
enum class script : std::uint8_t { other, greek, hebrew, hiragana, katakana, han };

[[nodiscard]] idna_property idna2008_property(char32_t cp) noexcept;
[[nodiscard]] bidi_class bidi(char32_t cp) noexcept;
[[nodiscard]] joining_type joining(char32_t cp) noexcept;
[[nodiscard]] script script_of(char32_t cp) noexcept;
/// General_Category is Mn, Mc or Me.
[[nodiscard]] bool is_mark(char32_t cp) noexcept;
[[nodiscard]] std::uint8_t combining_class(char32_t cp) noexcept;

/// UTS #46 status, with deviations treated as valid (nontransitional processing).
enum class uts46_status : std::uint8_t { valid, mapped, ignored, disallowed };

/// The UTS #46 status of @p cp. For `mapped`, the mapping is appended to @p mapping.
uts46_status uts46_map(char32_t cp, std::u32string& mapping);

/// Normalization Form C.
[[nodiscard]] std::u32string nfc(std::u32string_view s);

} // namespace jsonschema::detail::unicode
