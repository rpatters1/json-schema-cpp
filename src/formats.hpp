// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Built-in checkers for the formats defined by JSON Schema 2020-12 (validation §7.3,
// design doc 5.1). Each takes the UTF-8 text of a string instance.
#pragma once

#include <string_view>

namespace jsonschema::detail {

using format_function = bool (*)(std::string_view);

/// The built-in checker for a 2020-12 format name, or nullptr if the format is unknown.
[[nodiscard]] format_function find_builtin_format(std::string_view name) noexcept;

// Exposed for unit tests.
[[nodiscard]] bool is_date(std::string_view s) noexcept;
[[nodiscard]] bool is_time(std::string_view s) noexcept;
[[nodiscard]] bool is_date_time(std::string_view s) noexcept;
[[nodiscard]] bool is_duration(std::string_view s) noexcept;
[[nodiscard]] bool is_email(std::string_view s) noexcept;
[[nodiscard]] bool is_hostname(std::string_view s);
[[nodiscard]] bool is_idn_email(std::string_view s);
[[nodiscard]] bool is_iri(std::string_view s);
[[nodiscard]] bool is_iri_reference(std::string_view s);
[[nodiscard]] bool is_uuid(std::string_view s) noexcept;
[[nodiscard]] bool is_uri_template(std::string_view s) noexcept;
[[nodiscard]] bool is_relative_json_pointer(std::string_view s) noexcept;

} // namespace jsonschema::detail
