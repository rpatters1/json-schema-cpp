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

namespace {

std::string path_text(const std::vector<path_segment>& segments, std::size_t count)
{
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& s = segments[i];
        json_pointer::append(text, s.keyword);
        if (s.shape == path_segment::kind::with_key) {
            json_pointer::append(text, s.key);
        } else if (s.shape == path_segment::kind::with_index) {
            text += '/';
            text += std::to_string(s.index);
        } else if (s.shape == path_segment::kind::with_pointer) {
            text += s.key;
        }
    }
    return text;
}

std::string instance_text(const std::vector<instance_token>& tokens, std::size_t count)
{
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        if (tokens[i].is_index) {
            text += '/';
            text += std::to_string(tokens[i].index);
        } else {
            json_pointer::append(text, tokens[i].key);
        }
    }
    return text;
}

} // namespace

std::vector<output_unit> eval_context::current_chain() const
{
    std::vector<output_unit> units;
    units.reserve(chain_.size());
    for (const auto& c : chain_) {
        units.push_back({path_text(evaluation_path, c.evaluation_path_length), c.subschema->location,
                         instance_text(instance_path, c.instance_path_length)});
    }
    return units;
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
        } else if (s.shape == path_segment::kind::with_pointer) {
            text += s.key;
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
    error_entry e{instance_pointer(), evaluation_path_text(path_keyword), schema_location, keyword_name, std::move(message), &instance, {}};
    if (settings_.record_chains) {
        e.chain = current_chain();
    }
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
    if (settings_.captured_errors != nullptr) {
        settings_.captured_errors->push_back(e);
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
                          std::move(summary), &instance, {}};
            if (settings_.record_chains) {
                e.chain = current_chain();
            }
            errors_.insert(errors_.begin() + static_cast<std::ptrdiff_t>(m.errors), std::move(e));
        }
    }
    --open_conditionals_;
    if (settings_.errors != nullptr) {
        flush_errors();
    }
}

namespace {

via to_via(edge e)
{
    switch (e) {
    case edge::root:
        return via::root;
    case edge::child:
        return via::child;
    case edge::ref:
        return via::ref;
    case edge::in_place:
    case edge::none:
        break;
    }
    return via::in_place;
}

} // namespace

eval_context::match_branch::match_branch(eval_context& context, bool needed)
    : context_(context), parent_(context.current_branch_), active_(needed && context.classifying() && !context.silent_matches_)
{
    if (active_) context_.current_branch_ = this;
}

classification_branch_id eval_context::match_branch::announce()
{
    if (id_ == 0) {
        const auto parent_id = parent_ ? parent_->announce() : 0;
        id_ = context_.next_branch_++;
        context_.settings_.classifications->on_branch({branch_action::begin, id_, parent_id});
    }
    return id_;
}

classification_branch_id eval_context::match_branch::detach()
{
    if (!active_) return 0;
    context_.current_branch_ = parent_;
    active_ = false;
    return id_;
}

void eval_context::match_branch::finish(bool commit)
{
    context_.resolve_branch(detach(), commit);
}

void eval_context::resolve_branch(classification_branch_id id, bool commit, bool ambiguous)
{
    if (!id) return;
    const auto parent = current_branch_ ? current_branch_->announce() : 0;
    settings_.classifications->on_branch({commit ? branch_action::commit : branch_action::discard,
                                          id, parent, ambiguous ? &classification_location_ : nullptr});
}

void eval_context::report_match(const node& n, const json& instance, edge reached_by, confidence certainty)
{
    if (silent_matches_) return;
    const auto branch = current_branch_ ? current_branch_->announce() : 0;
    match m{n.name, n.location, to_via(reached_by), certainty, *n.info};
    settings_.classifications->on_classified({classification_location_, instance, {&m, 1},
                                              static_cast<classification_status>(certainty), branch});
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

    const std::size_t annotation_mark = context.annotations_.size();
    if (context.settings_.record_chains) {
        context.chain_.push_back({&n, context.evaluation_path.size(), context.instance_path.size()});
    }

    // `via` for classification: the strongest edge since the previous match (doc 4.3).
    const edge saved_via = context.via_;
    if (entered_by == edge::root || entered_by == edge::child) {
        context.via_ = entered_by;
    } else {
        context.via_ = std::max(context.via_, entered_by);
    }
    const auto reached_by = context.via_;
    const bool named = context.classifying() && !n.name.empty();
    if (named) context.via_ = edge::none;
    eval_context::match_branch decision(context, !context.recovering() || n.last_identity_check != nullptr);

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
            context.annotations_.push_back({context.instance_pointer(), context.evaluation_path_text(name), std::move(location), name, value,
                                            context.settings_.record_chains ? context.current_chain() : std::vector<output_unit>()});
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
        if (context.classifying() && context.recovering() && !context.identity_ && k.get() == n.last_identity_check) {
            decision.finish(true);
        }
    }
    if (valid && n.has_unevaluated && annotations != nullptr) {
        annotations->merge(own);
    }

    const bool identity = context.identity_;
    if (!valid) {
        context.truncate_annotations(annotation_mark);
    }
    const bool retained = valid || (context.recovering() && !identity);
    decision.finish(retained);
    if (named && retained) {
        context.report_match(n, instance, reached_by, valid ? confidence::exact : confidence::recovered);
    }
    context.identity_ = saved_identity;
    context.last_identity_ = !valid && identity;
    context.via_ = saved_via;
    if (context.settings_.record_chains) {
        context.chain_.pop_back();
    }
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
