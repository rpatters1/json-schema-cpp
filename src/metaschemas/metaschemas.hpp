// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The embedded JSON Schema 2020-12 meta-schemas (doc 2.3: built in, never loaded).
#pragma once

#include <optional>
#include <string_view>

namespace jsonschema::detail {

/// The text of a built-in meta-schema document, by its URI (normalized, no fragment).
[[nodiscard]] std::optional<std::string_view> builtin_document(std::string_view uri) noexcept;

} // namespace jsonschema::detail
