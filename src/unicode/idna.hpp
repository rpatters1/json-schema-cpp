// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Internationalized domain names (design doc 5.1, 5.4): Punycode (RFC 3492), IDNA2008
// label validity (RFC 5891 §5.4, RFC 5892, RFC 5893) and UTS #46 processing.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace jsonschema::detail::idna {

/// Decodes the Punycode part of an A-label (after "xn--"); nullopt if it is not valid
/// Punycode. Letters are accepted in either case.
[[nodiscard]] std::optional<std::u32string> punycode_decode(std::string_view encoded);

/// Encodes @p label as Punycode (without the "xn--" prefix); nullopt on overflow.
[[nodiscard]] std::optional<std::string> punycode_encode(std::u32string_view label);

/// `idn-hostname`: UTS #46 processing (nontransitional, UseSTD3ASCIIRules, CheckHyphens,
/// CheckBidi, CheckJoiners, VerifyDnsLength), with every resulting label also required
/// to be a valid IDNA2008 U-label or A-label. Label separators are the full stop and the
/// characters UTS #46 maps to it (U+3002, U+FF0E, U+FF61).
[[nodiscard]] bool is_idn_hostname(std::string_view utf8);

/// For `hostname`: true if every label of @p ascii_hostname that begins with "xn--"
/// (in any case) is a valid A-label, and the Bidi rule holds if any label is
/// right-to-left. Other labels are not checked here.
[[nodiscard]] bool has_valid_a_labels(std::string_view ascii_hostname);

} // namespace jsonschema::detail::idna
