// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// The keyword table. Each keyword file defines factories; keywords.cpp lists them.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "compiler.hpp"

namespace jsonschema::detail {

/// Evaluation order within a node (doc 2.4). Assertions come first so the fast path
/// fails early and parent errors precede child errors; `unevaluated*` must come last.
enum class keyword_phase : std::uint8_t
{
    assertion,
    reference,
    in_place,
    child,
    unevaluated
};

/// Returns the compiled keyword, or nullptr if it needs no evaluation (for example
/// `then`, which `if` compiles, or a `contains` with `minContains: 0` and no maximum).
using keyword_factory = std::unique_ptr<keyword> (*)(const compile_scope&);

struct keyword_definition
{
    std::string_view name;
    keyword_phase phase;
    keyword_factory factory;
};

/// The definition of a 2020-12 keyword that needs compiling, or nullptr.
[[nodiscard]] const keyword_definition* find_keyword(std::string_view name);

// core.cpp
std::unique_ptr<keyword> make_ref(const compile_scope&);
std::unique_ptr<keyword> make_dynamic_ref(const compile_scope&);

// applicator.cpp
std::unique_ptr<keyword> make_all_of(const compile_scope&);
std::unique_ptr<keyword> make_any_of(const compile_scope&);
std::unique_ptr<keyword> make_one_of(const compile_scope&);
std::unique_ptr<keyword> make_not(const compile_scope&);
std::unique_ptr<keyword> make_if(const compile_scope&);
std::unique_ptr<keyword> make_dependent_schemas(const compile_scope&);
std::unique_ptr<keyword> make_properties(const compile_scope&);
std::unique_ptr<keyword> make_pattern_properties(const compile_scope&);
std::unique_ptr<keyword> make_additional_properties(const compile_scope&);
std::unique_ptr<keyword> make_property_names(const compile_scope&);
std::unique_ptr<keyword> make_prefix_items(const compile_scope&);
std::unique_ptr<keyword> make_items(const compile_scope&);
std::unique_ptr<keyword> make_contains(const compile_scope&);

// validation.cpp
std::unique_ptr<keyword> make_type(const compile_scope&);
std::unique_ptr<keyword> make_enum(const compile_scope&);
std::unique_ptr<keyword> make_const(const compile_scope&);
std::unique_ptr<keyword> make_multiple_of(const compile_scope&);
std::unique_ptr<keyword> make_maximum(const compile_scope&);
std::unique_ptr<keyword> make_exclusive_maximum(const compile_scope&);
std::unique_ptr<keyword> make_minimum(const compile_scope&);
std::unique_ptr<keyword> make_exclusive_minimum(const compile_scope&);
std::unique_ptr<keyword> make_max_length(const compile_scope&);
std::unique_ptr<keyword> make_min_length(const compile_scope&);
std::unique_ptr<keyword> make_pattern(const compile_scope&);
std::unique_ptr<keyword> make_max_items(const compile_scope&);
std::unique_ptr<keyword> make_min_items(const compile_scope&);
std::unique_ptr<keyword> make_unique_items(const compile_scope&);
std::unique_ptr<keyword> make_max_properties(const compile_scope&);
std::unique_ptr<keyword> make_min_properties(const compile_scope&);
std::unique_ptr<keyword> make_required(const compile_scope&);
std::unique_ptr<keyword> make_dependent_required(const compile_scope&);

/// Helpers shared by keyword files.
/// A non-negative integer keyword value (`1.0` is accepted, values above 2^64-1 saturate).
[[nodiscard]] std::uint64_t non_negative_integer(const compile_scope& scope);
/// The keyword's value as an array of subschemas; fails if it is not a non-empty array of schemas.
[[nodiscard]] std::vector<const node*> subschema_array(const compile_scope& scope);

} // namespace jsonschema::detail
