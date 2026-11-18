// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// RFC 6901 JSON Pointer helpers that work on unescaped string tokens, without the
// exceptions nlohmann::json_pointer throws for missing members.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace jsonschema::detail::json_pointer {

using json = nlohmann::json;

/// Escapes one reference token ("~" -> "~0", "/" -> "~1").
[[nodiscard]] std::string escape(std::string_view token);

/// Appends "/" + escape(token) to @p pointer.
void append(std::string& pointer, std::string_view token);

/// Splits @p pointer into unescaped tokens. Returns std::nullopt if it is not a valid
/// JSON pointer (non-empty and not starting with "/", or "~" not followed by 0 or 1).
[[nodiscard]] std::optional<std::vector<std::string>> parse(std::string_view pointer);

/// Follows @p pointer from @p root. Returns nullptr if the pointer is invalid or does not
/// identify a value. Array indexes must be decimal with no leading zeros; "-" never matches.
[[nodiscard]] const json* navigate(const json& root, std::string_view pointer);

} // namespace jsonschema::detail::json_pointer
