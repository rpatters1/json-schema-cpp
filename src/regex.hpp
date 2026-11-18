// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Regular expressions for `pattern` and `patternProperties` (design doc 5.2).
//
// Milestone 1 uses std::regex in ECMAScript mode as an interim backend. It works on
// UTF-8 bytes rather than code points and lacks Unicode property classes, so the
// optional ecmascript-regex tests are not expected to pass until the PCRE2 backend
// (milestone 4) replaces it behind this same interface.
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
    /// Safe to call concurrently.
    [[nodiscard]] virtual bool search(std::string_view utf8) const = 0;
};

/// Compiles an ECMA-262 pattern. Returns nullptr if the pattern is invalid.
[[nodiscard]] std::unique_ptr<compiled_regex> compile_regex(std::string_view pattern);

} // namespace jsonschema::detail
