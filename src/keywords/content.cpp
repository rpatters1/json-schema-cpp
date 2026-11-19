// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// `contentEncoding`, `contentMediaType` and `contentSchema` in assertion mode (design doc
// 5.5). They are annotations otherwise; the compiler records those either way.
#include <optional>
#include <string>

#include "content.hpp"
#include "json/access.hpp"
#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace ja = json_access;

namespace {

/// The three keywords of one schema object, evaluated together: decode, then parse, then
/// validate the parsed value. Each reports errors at its own keyword location.
class content_keyword final : public keyword
{
public:
    struct part
    {
        std::string name;           ///< the encoding or media type, as written
        std::string location;       ///< absolute keyword location
    };

    content_keyword(std::optional<part> encoding, content_decoder decoder, std::optional<part> media_type, content_parser parser,
                    std::optional<part> schema, const node* content_schema)
        : encoding_(std::move(encoding)), decoder_(std::move(decoder)), media_type_(std::move(media_type)), parser_(std::move(parser)),
          schema_(std::move(schema)), content_schema_(content_schema)
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_string()) {
            return true;
        }
        std::string_view content = ja::string_view_of(instance);
        std::optional<std::string> decoded;
        if (decoder_) {
            decoded = decoder_(content);
            if (!decoded) {
                if (context.reporting_errors()) {
                    context.add_error("contentEncoding", encoding_->location, instance, "the string is not valid " + encoding_->name);
                }
                return false;
            }
            content = *decoded;
        }
        if (!parser_) {
            return true;
        }
        const std::optional<json> parsed = parser_(content);
        if (!parsed) {
            if (context.reporting_errors()) {
                context.add_error("contentMediaType", media_type_->location, instance,
                                  std::string(decoder_ ? "the decoded content" : "the string") + " is not valid " + media_type_->name);
            }
            return false;
        }
        if (content_schema_ == nullptr) {
            return true;
        }
        // The parsed value is not part of the instance, so errors cannot refer to it (as for
        // `propertyNames`): one error on the string carries the first inner reason.
        const auto mark = context.begin_conditional();
        bool valid;
        {
            const eval_context::silence_matches silent(context);
            const path_scope scope(context, segment("contentSchema"));
            valid = detail::evaluate(*content_schema_, *parsed, context, nullptr, edge::child);
        }
        const std::string reason = valid ? std::string() : context.first_error_since(mark);
        context.truncate_annotations(mark.annotations);
        context.end_conditional(mark, false, *this, instance, {});
        if (!valid && context.reporting_errors()) {
            context.add_error("contentSchema", schema_->location, instance,
                              "the content does not match contentSchema" + (reason.empty() ? std::string() : ": " + reason));
        }
        return valid;
    }

private:
    std::optional<part> encoding_;
    content_decoder decoder_;
    std::optional<part> media_type_;
    content_parser parser_;
    std::optional<part> schema_;
    const node* content_schema_;
};

content_decoder find_decoder(const compile_options& options, std::string_view name)
{
    for (const auto& [n, decoder] : options.extra_content_encodings) {
        if (equal_ignoring_case(n, name)) {
            return decoder;
        }
    }
    if (const content_decode_function builtin = find_builtin_encoding(name)) {
        return builtin;
    }
    return {};
}

content_parser find_parser(const compile_options& options, std::string_view media_type)
{
    const std::string normalized = normalize_media_type(media_type);
    for (const auto& [n, parser] : options.extra_media_types) {
        if (normalize_media_type(n) == normalized) {
            return parser;
        }
    }
    if (is_json_media_type(normalized)) {
        return [](std::string_view text) -> std::optional<json> {
            json parsed = json::parse(text.begin(), text.end(), nullptr, false);
            if (parsed.is_discarded()) {
                return std::nullopt;
            }
            return parsed;
        };
    }
    return {};
}

std::optional<content_keyword::part> part_of(const compile_scope& scope, std::string_view name)
{
    const json* value = scope.sibling(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    const compile_scope sibling = scope.for_sibling(name, *value);
    if (name != "contentSchema" && !value->is_string()) {
        sibling.fail(std::string(name) + " must be a string");
    }
    return content_keyword::part{value->is_string() ? value->get<std::string>() : std::string(), sibling.location()};
}

/// Compiles the content keywords of the schema object, from whichever of them owns the group.
std::unique_ptr<keyword> make_content(const compile_scope& scope)
{
    if (scope.options().content != content_mode::assert) {
        return nullptr;
    }
    const compile_options& options = scope.options();
    auto encoding = part_of(scope, "contentEncoding");
    auto media_type = part_of(scope, "contentMediaType");
    content_decoder decoder;
    if (encoding) {
        decoder = find_decoder(options, encoding->name);
        if (!decoder) {
            return nullptr;     // an unknown encoding: the content cannot be checked at all
        }
    }
    content_parser parser;
    std::optional<content_keyword::part> schema;
    const node* content_schema = nullptr;
    if (media_type) {
        parser = find_parser(options, media_type->name);
        if (parser) {
            if (const json* value = scope.sibling("contentSchema")) {
                schema = part_of(scope, "contentSchema");
                content_schema = scope.for_sibling("contentSchema", *value).subschema(*value);
            }
        }
    }
    if (!decoder && !parser) {
        return nullptr;
    }
    return std::make_unique<content_keyword>(std::move(encoding), std::move(decoder), std::move(media_type), std::move(parser),
                                             std::move(schema), content_schema);
}

} // namespace

std::unique_ptr<keyword> make_content_encoding(const compile_scope& scope)
{
    return make_content(scope);
}

std::unique_ptr<keyword> make_content_media_type(const compile_scope& scope)
{
    // With an encoding, `contentEncoding` compiles the group.
    return scope.sibling("contentEncoding") != nullptr ? nullptr : make_content(scope);
}

} // namespace jsonschema::detail
