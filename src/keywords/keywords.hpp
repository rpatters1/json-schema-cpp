// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
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

/// The 2020-12 vocabularies, as bits (doc 3: `$vocabulary`).
enum vocabulary : unsigned
{
    vocab_core = 1u << 0,
    vocab_applicator = 1u << 1,
    vocab_unevaluated = 1u << 2,
    vocab_validation = 1u << 3,
    vocab_meta_data = 1u << 4,
    vocab_format_annotation = 1u << 5,
    vocab_content = 1u << 6,
    vocab_all_2020_12 = (1u << 7) - 1,     ///< the vocabularies of the standard meta-schema
    vocab_format_assertion = 1u << 7,       ///< only when a meta-schema lists it
};

/// The bit for a vocabulary URI, or 0 if it is not one this library implements.
[[nodiscard]] unsigned vocabulary_bit(std::string_view uri);

/// Returns the compiled keyword, or nullptr if it needs no evaluation (for example
/// `then`, which `if` compiles, or a `contains` with `minContains: 0` and no maximum).
using keyword_factory = std::unique_ptr<keyword> (*)(const compile_scope&);

struct keyword_definition
{
    std::string_view name;
    keyword_phase phase;
    keyword_factory factory;
    unsigned vocabulary;    ///< a `vocabulary` bit
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
std::unique_ptr<keyword> make_dependencies(const compile_scope&);
std::unique_ptr<keyword> make_properties(const compile_scope&);
std::unique_ptr<keyword> make_pattern_properties(const compile_scope&);
std::unique_ptr<keyword> make_additional_properties(const compile_scope&);
std::unique_ptr<keyword> make_property_names(const compile_scope&);
std::unique_ptr<keyword> make_prefix_items(const compile_scope&);
std::unique_ptr<keyword> make_items(const compile_scope&);
std::unique_ptr<keyword> make_contains(const compile_scope&);
std::unique_ptr<keyword> make_unevaluated_properties(const compile_scope&);
std::unique_ptr<keyword> make_unevaluated_items(const compile_scope&);

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
std::unique_ptr<keyword> make_format(const compile_scope&);

// content.cpp
std::unique_ptr<keyword> make_content_encoding(const compile_scope&);
std::unique_ptr<keyword> make_content_media_type(const compile_scope&);

// extension.cpp
/// Compiles one occurrence of a custom keyword; sets @p phase from the subschemas it uses.
std::unique_ptr<keyword> make_extension(const compile_scope& scope, const keyword_extension& extension, keyword_phase& phase);

/// True if @p name is a keyword of JSON Schema 2020-12 (any vocabulary, including the
/// superseded `definitions` and `dependencies`).
[[nodiscard]] bool is_2020_12_keyword(std::string_view name);

/// Helpers shared by keyword files.
/// A non-negative integer keyword value (`1.0` is accepted, values above 2^64-1 saturate).
[[nodiscard]] std::uint64_t non_negative_integer(const compile_scope& scope);
/// The keyword's value as an array of subschemas; fails if it is not a non-empty array of schemas.
[[nodiscard]] std::vector<const node*> subschema_array(const compile_scope& scope);

} // namespace jsonschema::detail
