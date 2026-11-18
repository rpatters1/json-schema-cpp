// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>

#include "json/pointer.hpp"
#include "node.hpp"

namespace jsonschema::detail {

eval_context::eval_context(const settings& s) : settings_(s) {}

json::json_pointer eval_context::instance_pointer() const
{
    json::json_pointer result;
    for (const auto& token : instance_path) {
        if (token.is_index) {
            result.push_back(std::to_string(token.index));
        } else {
            result.push_back(std::string(token.key));
        }
    }
    return result;
}

std::string eval_context::evaluation_path_text(std::string_view last_keyword) const
{
    std::string text;
    for (const auto& s : evaluation_path) {
        json_pointer::append(text, s.keyword);
        if (s.shape == path_segment::kind::with_key) {
            json_pointer::append(text, s.key);
        } else if (s.shape == path_segment::kind::with_index) {
            text += '/';
            text += std::to_string(s.index);
        }
    }
    if (!last_keyword.empty()) {
        json_pointer::append(text, last_keyword);
    }
    return text;
}

void eval_context::add_error(std::string_view keyword_name, std::string_view schema_location, const json& instance,
                             std::string message)
{
    if (error_limit_reached_) {
        return;
    }
    // The `false` schema is not a keyword: its evaluation path is the schema's own path.
    const std::string_view path_keyword = keyword_name == "false" ? std::string_view() : keyword_name;
    error_entry e{instance_pointer(), evaluation_path_text(path_keyword), schema_location, keyword_name, std::move(message), &instance};
    if (open_conditionals_ == 0) {
        deliver(e);
    } else {
        errors_.push_back(std::move(e));
    }
}

void eval_context::deliver(const error_entry& e)
{
    if (error_limit_reached_) {
        return;
    }
    const error public_error{e.instance_location, e.evaluation_path, std::string(e.schema_location), e.keyword, e.message, e.instance};
    ++delivered_errors_;
    if (settings_.max_errors != 0 && delivered_errors_ >= settings_.max_errors) {
        error_limit_reached_ = true;
    }
    settings_.errors->on_error(public_error);
}

void eval_context::flush_errors()
{
    if (open_conditionals_ != 0) {
        return;
    }
    for (const auto& e : errors_) {
        deliver(e);
    }
    errors_.clear();
}

eval_context::mark eval_context::begin_conditional()
{
    ++open_conditionals_;
    return current_mark();
}

void eval_context::end_conditional(const mark& m, bool keep_errors, const keyword& source, const json& instance,
                                   std::string summary)
{
    if (settings_.errors != nullptr) {
        if (!keep_errors) {
            errors_.resize(m.errors);
        } else if (!summary.empty()) {
            error_entry e{instance_pointer(), evaluation_path_text(source.name()), source.location(), source.name(),
                          std::move(summary), &instance};
            errors_.insert(errors_.begin() + static_cast<std::ptrdiff_t>(m.errors), std::move(e));
        }
    }
    --open_conditionals_;
    if (settings_.errors != nullptr) {
        flush_errors();
    }
}

bool evaluate(const node& n, const json& instance, eval_context& context, frame* annotations, edge entered_by)
{
    if (n.always_false) {
        if (context.reporting_errors()) {
            context.add_error("false", n.location, instance, "the schema is false, so no value is valid here");
        }
        context.last_identity_ = false;
        return false;
    }
    const bool entered = context.enter_resource(n.resource);

    // Fast path: nothing to report and no annotations wanted here.
    if (!context.tracking_paths() && !context.classifying() && annotations == nullptr && !n.has_unevaluated) {
        bool valid = true;
        for (const auto& k : n.keywords) {
            if (!k->evaluate(instance, context, nullptr)) {
                valid = false;
                break;
            }
        }
        if (entered) {
            context.leave_resource();
        }
        context.last_identity_ = false;
        return valid;
    }

    const std::size_t classification_mark = context.classifications_.size();
    const std::size_t annotation_mark = context.annotations_.size();

    // `via` for classification: the strongest edge since the previous match (doc 4.3).
    const edge saved_via = context.via_;
    if (entered_by == edge::root || entered_by == edge::child) {
        context.via_ = entered_by;
    } else {
        context.via_ = std::max(context.via_, entered_by);
    }
    std::size_t own_entry = static_cast<std::size_t>(-1);
    if (context.classifying() && !n.name.empty()) {
        own_entry = context.classifications_.size();
        context.classifications_.push_back({&instance, &n, context.via_, confidence::exact});
        context.via_ = edge::none;
    }

    if (context.settings_.annotations && !n.annotations.empty()) {
        for (const auto& [name, value] : n.annotations) {
            // The content keywords annotate strings only, and contentSchema only together
            // with contentMediaType (2020-12 validation §8).
            if (name.starts_with("content") && (name == "contentEncoding" || name == "contentMediaType" || name == "contentSchema")) {
                if (!instance.is_string() || (name == "contentSchema" && !n.schema->contains("contentMediaType"))) {
                    continue;
                }
            }
            std::string location = n.location;
            location += '/';
            location += encode_fragment(json_pointer::escape(name));
            context.annotations_.push_back(
                {context.instance_pointer(), context.evaluation_path_text(name), std::move(location), name, value});
        }
    }

    const bool saved_identity = context.identity_;
    context.identity_ = false;

    frame own;
    frame* target = n.has_unevaluated ? &own : annotations;
    bool valid = true;
    for (const auto& k : n.keywords) {
        if (!k->evaluate(instance, context, target)) {
            valid = false;
            if (!context.exhaustive()) {
                break;
            }
        }
    }
    if (valid && n.has_unevaluated && annotations != nullptr) {
        annotations->merge(own);
    }

    const bool identity = context.identity_;
    if (!valid) {
        context.truncate_annotations(annotation_mark);
        if (context.classifying()) {
            if (!context.recovering() || identity) {
                context.truncate_classifications(classification_mark);
            } else if (own_entry != static_cast<std::size_t>(-1)) {
                context.classifications_[own_entry].confidence = confidence::recovered;
            }
        }
    }
    context.identity_ = saved_identity;
    context.last_identity_ = !valid && identity;
    context.via_ = saved_via;
    if (entered) {
        context.leave_resource();
    }
    return valid;
}

std::string encode_fragment(std::string_view pointer)
{
    // RFC 3986 fragment: pchar / "/" / "?", where pchar is unreserved / pct-encoded /
    // sub-delims / ":" / "@". Everything else, including "%", is percent-encoded.
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(pointer.size());
    for (const char c : pointer) {
        const auto u = static_cast<unsigned char>(c);
        const bool allowed = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
                             std::string_view("-._~!$&'()*+,;=:@/?").find(c) != std::string_view::npos;
        if (allowed) {
            result += c;
        } else {
            result += '%';
            result += hex[u >> 4];
            result += hex[u & 0xF];
        }
    }
    return result;
}

std::string_view type_name(const json& instance)
{
    switch (instance.type()) {
    case json::value_t::null:
        return "null";
    case json::value_t::boolean:
        return "boolean";
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        return "integer";
    case json::value_t::number_float: {
        const double d = instance.get<double>();
        return std::isfinite(d) && std::trunc(d) == d ? "integer" : "number";
    }
    case json::value_t::string:
        return "string";
    case json::value_t::array:
        return "array";
    case json::value_t::object:
        return "object";
    default:
        return "unsupported value";
    }
}

std::string brief(const json& value)
{
    constexpr std::size_t limit = 60;
    std::string text = value.dump();
    if (text.size() > limit) {
        text.resize(limit - 3);
        text += "...";
    }
    return text;
}

} // namespace jsonschema::detail
