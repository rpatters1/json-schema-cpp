// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Built-in support for content assertion (design doc 5.5): the RFC 4648 encodings for
// `contentEncoding` and JSON media types for `contentMediaType`.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace jsonschema::detail {

using content_decode_function = std::optional<std::string> (*)(std::string_view encoded);

/// The built-in decoder for an encoding name (`base64`, `base64url`, `base32`, `base16`,
/// in any case), or nullptr.
[[nodiscard]] content_decode_function find_builtin_encoding(std::string_view name) noexcept;

/// A media type reduced for lookup: lower case, without parameters or surrounding spaces
/// (`Application/JSON; charset=utf-8` becomes `application/json`).
[[nodiscard]] std::string normalize_media_type(std::string_view media_type);

/// True for `application/json` and any `+json` structured syntax suffix. @p media_type
/// must be normalized.
[[nodiscard]] bool is_json_media_type(std::string_view media_type) noexcept;

/// True if @p a and @p b are equal ignoring ASCII case.
[[nodiscard]] bool equal_ignoring_case(std::string_view a, std::string_view b) noexcept;

// RFC 4648 decoders; nullopt if the text is not valid in the encoding. Padding is
// required where the encoding defines it, and nothing outside the alphabet is allowed.
[[nodiscard]] std::optional<std::string> decode_base64(std::string_view encoded);
[[nodiscard]] std::optional<std::string> decode_base64url(std::string_view encoded);
[[nodiscard]] std::optional<std::string> decode_base32(std::string_view encoded);
[[nodiscard]] std::optional<std::string> decode_base16(std::string_view encoded);

} // namespace jsonschema::detail
