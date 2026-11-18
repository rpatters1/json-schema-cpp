// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Regular expressions for `pattern`, `patternProperties` and the `regex` format
// (design doc 5.2). ECMA-262 syntax, evaluated as if the "u" flag were set.
//
// Backends, chosen at build time with JSONSCHEMA_REGEX:
// - srell (default): SRELL, a header-only ECMAScript engine (third_party/srell). Exact
//   ECMA-262 semantics on code points, including \p{...}.
// - std: std::regex in ECMAScript mode. Works on UTF-8 bytes, has no \p{...}, and its
//   details differ between standard libraries.
#pragma once

#include <memory>
#include <string_view>

namespace jsonschema::detail {

class compiled_regex
{
public:
    compiled_regex() = default;
    compiled_regex(const compiled_regex&) = delete;
    compiled_regex& operator=(const compiled_regex&) = delete;
    virtual ~compiled_regex() = default;

    /// True if the pattern matches anywhere in @p utf8 (patterns are not implicitly anchored).
    /// Safe to call concurrently. Throws `evaluation_error` if the engine gives up on a
    /// pathological pattern and input (SRELL's backtracking limit).
    [[nodiscard]] virtual bool search(std::string_view utf8) const = 0;
};

/// Compiles an ECMA-262 pattern. Returns nullptr if the pattern is invalid.
[[nodiscard]] std::unique_ptr<compiled_regex> compile_regex(std::string_view pattern);

} // namespace jsonschema::detail
