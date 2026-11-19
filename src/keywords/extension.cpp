// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Custom keywords (design doc 3.5): the adapter between `custom_keyword` and the evaluator.
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "json/pointer.hpp"
#include "keywords/keywords.hpp"

namespace jsonschema::detail {

/// What `keyword_compile_scope` collects while a custom keyword compiles; owned by the
/// compiled keyword afterwards, so `subschema` handles stay valid.
struct extension_state
{
    std::deque<std::string> paths;          ///< stable addresses, referenced by `subschema`
    std::vector<const node*> in_place;
    bool children = false;
};

class extension_keyword final : public keyword
{
public:
    extension_keyword(std::unique_ptr<custom_keyword> impl, std::unique_ptr<extension_state> state)
        : impl_(std::move(impl)), state_(std::move(state))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        keyword_eval_scope scope(context, *this, instance);
        return impl_->evaluate(instance, scope);
    }

    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), state_->in_place.begin(), state_->in_place.end());
    }

    [[nodiscard]] keyword_phase phase() const noexcept
    {
        return !state_->in_place.empty() ? keyword_phase::in_place : state_->children ? keyword_phase::child : keyword_phase::assertion;
    }

private:
    std::unique_ptr<custom_keyword> impl_;
    std::unique_ptr<extension_state> state_;
};

namespace {

/// The JSON pointer from @p root to the value at @p target's address, if it is inside @p root.
bool find_path(const json& root, const json* target, std::string& path)
{
    if (&root == target) {
        return true;
    }
    const std::size_t length = path.size();
    if (root.is_object()) {
        for (auto it = root.begin(); it != root.end(); ++it) {
            json_pointer::append(path, it.key());
            if (find_path(it.value(), target, path)) {
                return true;
            }
            path.resize(length);
        }
    } else if (root.is_array()) {
        for (std::size_t i = 0; i < root.size(); ++i) {
            json_pointer::append(path, std::to_string(i));
            if (find_path(root[i], target, path)) {
                return true;
            }
            path.resize(length);
        }
    }
    return false;
}

} // namespace

std::unique_ptr<keyword> make_extension(const compile_scope& scope, const keyword_extension& extension, keyword_phase& phase)
{
    auto state = std::make_unique<extension_state>();
    std::unique_ptr<custom_keyword> impl;
    try {
        const keyword_compile_scope public_scope(scope, *state);
        impl = extension.compile(public_scope);
    } catch (const schema_error&) {
        throw;
    } catch (const std::exception& e) {
        scope.fail(e.what());
    }
    if (!impl) {
        return nullptr;
    }
    auto result = std::make_unique<extension_keyword>(std::move(impl), std::move(state));
    phase = result->phase();
    return result;
}

} // namespace jsonschema::detail

namespace jsonschema {

using detail::compile_scope;

// --- keyword_compile_scope ------------------------------------------------------------------

const json& keyword_compile_scope::value() const noexcept
{
    return scope_.value();
}

const json& keyword_compile_scope::schema() const noexcept
{
    return scope_.schema();
}

std::string keyword_compile_scope::location() const
{
    return scope_.location();
}

void keyword_compile_scope::fail(std::string_view message) const
{
    scope_.fail(message);
}

subschema keyword_compile_scope::in_place_subschema(const json& value) const
{
    return compile(value, true);
}

subschema keyword_compile_scope::child_subschema(const json& value) const
{
    return compile(value, false);
}

subschema keyword_compile_scope::compile(const json& value, bool in_place) const
{
    std::string path;
    if (!detail::find_path(scope_.value(), &value, path)) {
        scope_.fail("a custom keyword's subschema must be part of the keyword's value");
    }
    if (!value.is_object() && !value.is_boolean()) {
        scope_.fail("expected a schema (an object or a boolean)" + (path.empty() ? std::string() : " at " + path));
    }
    std::string relative;
    detail::json_pointer::append(relative, scope_.keyword_name());
    scope_.schemas().discover_below(scope_.schema(), value, relative + path);
    const detail::node* n = scope_.subschema(value);
    const std::string& stored = state_.paths.emplace_back(std::move(path));
    if (in_place) {
        state_.in_place.push_back(n);
    } else {
        state_.children = true;
    }
    return subschema(n, &stored, in_place);
}

// --- keyword_eval_scope ---------------------------------------------------------------------

bool keyword_eval_scope::reporting_errors() const noexcept
{
    return context_.reporting_errors();
}

void keyword_eval_scope::fail(std::string message)
{
    context_.fail(keyword_, instance_, [&] { return std::move(message); });
}

bool keyword_eval_scope::apply(const subschema& s)
{
    return run(s, nullptr, {}, 0, true);
}

bool keyword_eval_scope::apply(const subschema& s, std::string_view property)
{
    const auto it = instance_.is_object() ? instance_.find(property) : instance_.end();
    if (!instance_.is_object() || it == instance_.end()) {
        throw std::invalid_argument("keyword_eval_scope::apply: the instance has no member \"" + std::string(property) + "\"");
    }
    return run(s, &*it, it.key(), 0, true);
}

bool keyword_eval_scope::apply(const subschema& s, std::size_t index)
{
    if (!instance_.is_array() || index >= instance_.size()) {
        throw std::invalid_argument("keyword_eval_scope::apply: the instance has no item " + std::to_string(index));
    }
    return run(s, &instance_[index], {}, index, true);
}

bool keyword_eval_scope::test(const subschema& s)
{
    return run(s, nullptr, {}, 0, false);
}

bool keyword_eval_scope::test(const subschema& s, std::string_view property)
{
    const auto it = instance_.is_object() ? instance_.find(property) : instance_.end();
    if (!instance_.is_object() || it == instance_.end()) {
        throw std::invalid_argument("keyword_eval_scope::test: the instance has no member \"" + std::string(property) + "\"");
    }
    return run(s, &*it, it.key(), 0, false);
}

bool keyword_eval_scope::test(const subschema& s, std::size_t index)
{
    if (!instance_.is_array() || index >= instance_.size()) {
        throw std::invalid_argument("keyword_eval_scope::test: the instance has no item " + std::to_string(index));
    }
    return run(s, &instance_[index], {}, index, false);
}

bool keyword_eval_scope::run(const subschema& s, const json* child, std::string_view key, std::size_t index, bool report)
{
    using namespace detail;
    if (!s) {
        throw std::invalid_argument("keyword_eval_scope: empty subschema");
    }
    if (child == nullptr && !s.in_place_) {
        throw std::invalid_argument("keyword_eval_scope: only an in_place_subschema can be applied to the instance itself");
    }
    // The evaluation path is the keyword, then the pointer to the subschema inside its value.
    const path_segment seg{keyword_.name(), *s.path_, 0, path_segment::kind::with_pointer};
    const auto evaluate_it = [&] {
        if (child == nullptr) {
            return evaluate_in_place(*s.node_, instance_, context_, nullptr, seg, edge::in_place);
        }
        const instance_token token = key.data() != nullptr ? member_token(key) : item_token(index);
        return evaluate_child(*s.node_, *child, context_, seg, token);
    };
    if (report) {
        return evaluate_it();
    }
    const auto mark = context_.begin_conditional();
    bool valid;
    {
        const eval_context::errors_off quiet(context_);
        const eval_context::silence_matches silent(context_);
        valid = evaluate_it();
    }
    context_.truncate_annotations(mark.annotations);
    context_.end_conditional(mark, false, keyword_, instance_, {});
    return valid;
}

} // namespace jsonschema
