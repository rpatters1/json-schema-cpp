// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// Applicator and unevaluated vocabularies, plus the superseded `dependencies`.
//
// Annotation rules (doc 2.5): unconditional in-place applicators (`allOf`, `then`/`else`,
// `dependentSchemas`) share the caller's frame, because their failure fails the node and the
// caller discards the frame. Conditional ones (`anyOf`, `oneOf`, `not`, `if`) evaluate each
// branch into a scratch frame and merge it only if the branch counts.
#include <algorithm>
#include <optional>
#include <unordered_set>

#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace {

std::string count_text(std::size_t n, std::string_view singular, std::string_view plural)
{
    return std::to_string(n) + " " + std::string(n == 1 ? singular : plural);
}

// ---------------------------------------------------------------------------
// In-place applicators
// ---------------------------------------------------------------------------

class all_of_keyword final : public keyword
{
public:
    explicit all_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        bool valid = true;
        for (std::size_t i = 0; i < subschemas_.size(); ++i) {
            if (!evaluate_in_place(*subschemas_[i], instance, context, annotations, segment(name(), i), edge::in_place)) {
                valid = false;
                if (!context.exhaustive()) {
                    break;
                }
            }
        }
        return valid;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }
    void collect_facts(node_facts& facts) const override
    {
        facts.always_applied.insert(facts.always_applied.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

/// Shared by `anyOf` and `oneOf`: what each branch did.
struct branch_outcome
{
    std::size_t first;      ///< classification window
    std::size_t last;
    bool valid;
    bool identity;          ///< failed with an identity failure (doc 4.6)
};

/// Evaluates the branches in order, with errors off, stopping early only when nothing
/// needs the rest.
template <typename StopFn>
std::vector<branch_outcome> evaluate_branches(const keyword& self, const std::vector<const node*>& subschemas, const json& instance,
                                              eval_context& context, frame* annotations, StopFn&& may_stop)
{
    const eval_context::errors_off quiet(context);
    std::vector<branch_outcome> outcomes;
    outcomes.reserve(subschemas.size());
    std::size_t passed = 0;
    for (std::size_t i = 0; i < subschemas.size(); ++i) {
        const std::size_t first = context.current_mark().classifications;
        frame scratch;
        bool valid;
        {
            const path_scope scope(context, segment(self.name(), i));
            valid = detail::evaluate(*subschemas[i], instance, context, annotations ? &scratch : nullptr, edge::in_place);
        }
        outcomes.push_back({first, context.current_mark().classifications, valid, !valid && context.last_failure_was_identity()});
        if (valid) {
            ++passed;
            if (annotations != nullptr) {
                annotations->merge(scratch);
            }
            if (annotations == nullptr && !context.collect_all() && may_stop(passed)) {
                break;
            }
        }
    }
    return outcomes;
}

/// Classification bookkeeping after `anyOf`/`oneOf` branches (doc 4.6).
void settle_branches(eval_context& context, const std::vector<branch_outcome>& outcomes, const json& instance, bool ambiguous_if_many_pass)
{
    if (!context.classifying()) {
        return;
    }
    const auto passed = static_cast<std::size_t>(std::count_if(outcomes.begin(), outcomes.end(), [](const auto& o) { return o.valid; }));
    if (passed > 0) {
        // Failed branches never classify when another branch passed.
        for (auto it = outcomes.rbegin(); it != outcomes.rend(); ++it) {
            if (!it->valid) {
                context.erase_classifications(it->first, it->last);
            }
        }
        if (passed > 1 && ambiguous_if_many_pass) {
            // Windows shift left as earlier failed windows are erased.
            std::size_t offset = 0;
            for (const auto& o : outcomes) {
                const std::size_t size = o.last - o.first;
                if (o.valid) {
                    context.mark_ambiguous(o.first - offset, o.first - offset + size, instance);
                } else {
                    offset += size;
                }
            }
        }
        return;
    }
    // Every branch failed. Those with an identity failure already removed their own entries;
    // the others survive as candidates.
    const auto candidates = static_cast<std::size_t>(std::count_if(outcomes.begin(), outcomes.end(), [](const auto& o) { return !o.identity; }));
    if (candidates > 1) {
        for (const auto& o : outcomes) {
            context.mark_ambiguous(o.first, o.last, instance);
        }
    }
}

bool all_identity(const std::vector<branch_outcome>& outcomes)
{
    return std::all_of(outcomes.begin(), outcomes.end(), [](const auto& o) { return o.identity; });
}

/// When every branch failed and errors are reported, re-runs the branches to collect their
/// errors. Annotations, classifications and frames came from the first run, so this run's are dropped.
void collect_branch_errors(const keyword& self, const std::vector<const node*>& subschemas, const json& instance, eval_context& context)
{
    if (!context.reporting_errors()) {
        return;
    }
    for (std::size_t i = 0; i < subschemas.size(); ++i) {
        const auto before = context.current_mark();
        {
            const path_scope scope(context, segment(self.name(), i));
            static_cast<void>(detail::evaluate(*subschemas[i], instance, context, nullptr, edge::in_place));
        }
        context.truncate_classifications(before.classifications);
        context.truncate_annotations(before.annotations);
    }
}

class any_of_keyword final : public keyword
{
public:
    explicit any_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        const auto mark = context.begin_conditional();
        const auto outcomes = evaluate_branches(*this, subschemas_, instance, context, annotations, [](std::size_t) { return true; });
        const bool valid = std::any_of(outcomes.begin(), outcomes.end(), [](const auto& o) { return o.valid; });
        settle_branches(context, outcomes, instance, false);
        if (!valid) {
            if (all_identity(outcomes)) {
                context.identity_failure();
            }
            collect_branch_errors(*this, subschemas_, instance, context);
        }
        context.end_conditional(mark, !valid, *this, instance,
                                valid ? std::string() : "the value matches none of the " + std::to_string(subschemas_.size()) + " subschemas of anyOf");
        return valid;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

class one_of_keyword final : public keyword
{
public:
    explicit one_of_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        const auto mark = context.begin_conditional();
        const auto outcomes = evaluate_branches(*this, subschemas_, instance, context, annotations, [](std::size_t passed) { return passed > 1; });
        std::vector<std::size_t> passing;
        for (std::size_t i = 0; i < outcomes.size(); ++i) {
            if (outcomes[i].valid) {
                passing.push_back(i);
            }
        }
        settle_branches(context, outcomes, instance, true);
        if (passing.size() == 1) {
            context.end_conditional(mark, false, *this, instance, {});
            return true;
        }
        if (passing.empty()) {
            if (all_identity(outcomes)) {
                context.identity_failure();
            }
            collect_branch_errors(*this, subschemas_, instance, context);
            context.end_conditional(mark, true, *this, instance,
                                    "the value matches none of the " + std::to_string(subschemas_.size()) + " subschemas of oneOf");
            return false;
        }
        // More than one passed: the failing branches' errors are irrelevant.
        context.end_conditional(mark, false, *this, instance, {});
        context.fail(*this, instance, [&] {
            std::string text = "the value matches more than one subschema of oneOf (";
            for (std::size_t i = 0; i < passing.size(); ++i) {
                text += (i == 0 ? "" : ", ") + std::to_string(passing[i]);
            }
            return text + (outcomes.size() < subschemas_.size() ? ", ...)" : ")");
        });
        return false;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        out.insert(out.end(), subschemas_.begin(), subschemas_.end());
    }

private:
    std::vector<const node*> subschemas_;
};

class not_keyword final : public keyword
{
public:
    explicit not_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        const auto mark = context.begin_conditional();
        bool matched;
        {
            // If the subschema fails, its errors don't matter; if it passes, it has none.
            const eval_context::errors_off quiet(context);
            const path_scope scope(context, segment(name()));
            matched = detail::evaluate(*subschema_, instance, context, nullptr, edge::in_place);
        }
        // `not` never contributes annotations or classifications.
        context.truncate_classifications(mark.classifications);
        context.truncate_annotations(mark.annotations);
        context.end_conditional(mark, matched, *this, instance, matched ? "the value must not match the subschema of not" : std::string());
        return !matched;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override { out.push_back(subschema_); }

private:
    const node* subschema_;
};

class if_keyword final : public keyword
{
public:
    if_keyword(const node* condition, const node* then_branch, const node* else_branch)
        : condition_(condition), then_(then_branch), else_(else_branch)
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (then_ == nullptr && else_ == nullptr && annotations == nullptr && !context.collect_all()) {
            return true;    // `if` alone matters only for its annotations
        }
        const auto mark = context.begin_conditional();
        frame scratch;
        bool matched;
        {
            const eval_context::errors_off quiet(context);    // errors in `if` never count
            const path_scope scope(context, segment("if"));
            matched = detail::evaluate(*condition_, instance, context, annotations ? &scratch : nullptr, edge::in_place);
        }
        if (!matched) {
            context.truncate_classifications(mark.classifications);
        } else if (annotations != nullptr) {
            annotations->merge(scratch);
        }
        context.end_conditional(mark, false, *this, instance, {});   // errors in `if` never count
        const node* branch = matched ? then_ : else_;
        if (branch == nullptr) {
            return true;
        }
        return evaluate_in_place(*branch, instance, context, annotations, segment(matched ? "then" : "else"), edge::in_place);
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        for (const node* n : {condition_, then_, else_}) {
            if (n != nullptr) {
                out.push_back(n);
            }
        }
    }

private:
    const node* condition_;
    const node* then_;
    const node* else_;
};

class dependent_schemas_keyword final : public keyword
{
public:
    explicit dependent_schemas_keyword(std::vector<std::pair<std::string, const node*>> dependencies)
        : dependencies_(std::move(dependencies))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        for (const auto& [property, subschema] : dependencies_) {
            if (instance.contains(property) &&
                !evaluate_in_place(*subschema, instance, context, annotations, segment(name(), property), edge::in_place)) {
                valid = false;
                if (!context.exhaustive()) {
                    break;
                }
            }
        }
        return valid;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        for (const auto& entry : dependencies_) {
            out.push_back(entry.second);
        }
    }

private:
    std::vector<std::pair<std::string, const node*>> dependencies_;
};

/// The superseded `dependencies` keyword: each member is either an array of property
/// names (now `dependentRequired`) or a schema (now `dependentSchemas`).
class dependencies_keyword final : public keyword
{
public:
    struct dependency
    {
        std::string name;
        std::vector<std::string> required;  // array form
        const node* subschema = nullptr;    // schema form
    };

    explicit dependencies_keyword(std::vector<dependency> dependencies) : dependencies_(std::move(dependencies)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        for (const auto& d : dependencies_) {
            if (!instance.contains(d.name)) {
                continue;
            }
            if (d.subschema != nullptr) {
                if (!evaluate_in_place(*d.subschema, instance, context, annotations, segment(name(), d.name), edge::in_place)) {
                    valid = false;
                }
            } else {
                for (const auto& other : d.required) {
                    if (!instance.contains(other)) {
                        valid = false;
                        context.fail(*this, instance, [&] {
                            return "the property \"" + d.name + "\" requires the property \"" + other + "\", which is missing";
                        });
                        if (!context.exhaustive()) {
                            break;
                        }
                    }
                }
            }
            if (!valid && !context.exhaustive()) {
                break;
            }
        }
        return valid;
    }
    void in_place_subschemas(std::vector<const node*>& out) const override
    {
        for (const auto& d : dependencies_) {
            if (d.subschema != nullptr) {
                out.push_back(d.subschema);
            }
        }
    }

private:
    std::vector<dependency> dependencies_;
};

// ---------------------------------------------------------------------------
// Child applicators: objects
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, const node*>> subschema_map(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas");
    }
    std::vector<std::pair<std::string, const node*>> result;
    result.reserve(scope.value().size());
    for (const auto& [name, value] : scope.value().items()) {
        result.emplace_back(name, scope.subschema(value));
    }
    return result;
}

/// A property whose subschema is a `const` or a single-value `enum` (doc 4.6).
bool is_discriminator_schema(const json& subschema)
{
    if (!subschema.is_object()) {
        return false;
    }
    if (subschema.contains("const")) {
        return true;
    }
    const auto it = subschema.find("enum");
    return it != subschema.end() && it->is_array() && it->size() == 1;
}

class properties_keyword final : public keyword
{
public:
    struct property
    {
        std::string name;
        const node* subschema;
        bool discriminator;
    };

    explicit properties_keyword(std::vector<property> properties) : properties_(std::move(properties)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        const auto check = [&](const property& p, const json& value) {
            if (!evaluate_child(*p.subschema, value, context, segment(name(), p.name), member_token(p.name))) {
                valid = false;
                if (p.discriminator) {
                    context.identity_failure();
                }
                return context.exhaustive();
            }
            return true;
        };
        if (annotations == nullptr) {
            for (const auto& p : properties_) {
                const auto it = instance.find(p.name);
                if (it != instance.end() && !check(p, *it)) {
                    break;
                }
            }
            return valid;
        }
        // With a frame, walk the instance to learn member indexes. Both sequences are sorted.
        std::size_t index = 0;
        const std::size_t count = instance.size();
        for (auto it = instance.begin(); it != instance.end(); ++it, ++index) {
            const auto p = std::lower_bound(properties_.begin(), properties_.end(), it.key(),
                                            [](const property& x, const std::string& n) { return x.name < n; });
            if (p == properties_.end() || p->name != it.key()) {
                continue;
            }
            annotations->mark_property(index, count);
            if (!check(*p, it.value())) {
                break;
            }
        }
        return valid;
    }
    void collect_facts(node_facts& facts) const override
    {
        for (const auto& p : properties_) {
            facts.declared_properties.push_back(p.name);
        }
    }

private:
    std::vector<property> properties_;   // sorted by name
};

using pattern_list = std::vector<std::pair<std::string, std::shared_ptr<const compiled_regex>>>;

class pattern_properties_keyword final : public keyword
{
public:
    pattern_properties_keyword(pattern_list patterns, std::vector<const node*> subschemas)
        : patterns_(std::move(patterns)), subschemas_(std::move(subschemas))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        std::size_t index = 0;
        const std::size_t count = instance.size();
        for (auto it = instance.begin(); it != instance.end(); ++it, ++index) {
            for (std::size_t p = 0; p < patterns_.size(); ++p) {
                if (!patterns_[p].second->search(it.key())) {
                    continue;
                }
                if (annotations != nullptr) {
                    annotations->mark_property(index, count);
                }
                if (!evaluate_child(*subschemas_[p], it.value(), context, segment(name(), patterns_[p].first), member_token(it.key()))) {
                    valid = false;
                    if (!context.exhaustive()) {
                        return false;
                    }
                }
            }
        }
        return valid;
    }

private:
    pattern_list patterns_;
    std::vector<const node*> subschemas_;
};

/// Reports a member that a `false` subschema rejects, more helpfully than "the schema is false".
bool reject_member(const keyword& self, eval_context& context, const std::string& member, const json& value)
{
    const path_scope scope(context, member_token(member));
    context.fail(self, value, [&] { return "the property \"" + member + "\" is not allowed here (" + std::string(self.name()) + ")"; });
    return false;
}

class additional_properties_keyword final : public keyword
{
public:
    additional_properties_keyword(const node* subschema, std::unordered_set<std::string> names,
                                  std::vector<std::shared_ptr<const compiled_regex>> patterns)
        : subschema_(subschema), names_(std::move(names)), patterns_(std::move(patterns))
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        std::size_t index = 0;
        const std::size_t count = instance.size();
        for (auto it = instance.begin(); it != instance.end(); ++it, ++index) {
            const std::string& member = it.key();
            if (names_.contains(member) ||
                std::any_of(patterns_.begin(), patterns_.end(), [&](const auto& p) { return p->search(member); })) {
                continue;
            }
            if (annotations != nullptr) {
                annotations->mark_property(index, count);
            }
            const bool ok = subschema_->always_false
                                ? reject_member(*this, context, member, it.value())
                                : evaluate_child(*subschema_, it.value(), context, segment(name()), member_token(member));
            if (!ok) {
                valid = false;
                if (!context.exhaustive()) {
                    return false;
                }
            }
        }
        return valid;
    }

private:
    const node* subschema_;
    std::unordered_set<std::string> names_;
    std::vector<std::shared_ptr<const compiled_regex>> patterns_;
};

class property_names_keyword final : public keyword
{
public:
    explicit property_names_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* /*annotations*/) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        // A property name is not part of the instance, so errors about it cannot refer to it.
        // Each bad name is reported as one error on the object, carrying the inner reason.
        bool valid = true;
        for (auto it = instance.begin(); it != instance.end(); ++it) {
            const auto mark = context.begin_conditional();
            const json key(it.key());
            bool ok;
            {
                const path_scope scope(context, segment(name()));
                ok = detail::evaluate(*subschema_, key, context, nullptr, edge::child);
            }
            const std::string reason = ok ? std::string() : context.first_error_since(mark);
            // Property names are not instance locations: they are never classified or annotated.
            context.truncate_classifications(mark.classifications);
            context.truncate_annotations(mark.annotations);
            context.end_conditional(mark, false, *this, instance, {});
            if (!ok) {
                valid = false;
                context.fail(*this, instance, [&] {
                    return "the property name \"" + it.key() + "\" is not valid" + (reason.empty() ? std::string() : ": " + reason);
                });
                if (!context.exhaustive()) {
                    break;
                }
            }
        }
        return valid;
    }

private:
    const node* subschema_;
};

// ---------------------------------------------------------------------------
// Child applicators: arrays
// ---------------------------------------------------------------------------

class prefix_items_keyword final : public keyword
{
public:
    explicit prefix_items_keyword(std::vector<const node*> subschemas) : subschemas_(std::move(subschemas)) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        const std::size_t count = std::min(subschemas_.size(), instance.size());
        if (annotations != nullptr) {
            annotations->mark_prefix(count);
        }
        bool valid = true;
        for (std::size_t i = 0; i < count; ++i) {
            if (!evaluate_child(*subschemas_[i], instance[i], context, segment(name(), i), item_token(i))) {
                valid = false;
                if (!context.exhaustive()) {
                    break;
                }
            }
        }
        return valid;
    }

private:
    std::vector<const node*> subschemas_;
};

class items_keyword final : public keyword
{
public:
    items_keyword(const node* subschema, std::size_t start) : subschema_(subschema), start_(start) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        if (annotations != nullptr && instance.size() > start_) {
            annotations->all_items = true;
        }
        bool valid = true;
        for (std::size_t i = start_; i < instance.size(); ++i) {
            bool ok;
            if (subschema_->always_false) {
                const path_scope scope(context, item_token(i));
                context.fail(*this, instance[i], [&] {
                    return "the array may have at most " + count_text(start_, "item", "items") + ", but has " + std::to_string(instance.size());
                });
                ok = false;
            } else {
                ok = evaluate_child(*subschema_, instance[i], context, segment(name()), item_token(i));
            }
            if (!ok) {
                valid = false;
                if (!context.exhaustive() || subschema_->always_false) {
                    break;  // one "too many items" error is enough
                }
            }
        }
        return valid;
    }

private:
    const node* subschema_;
    std::size_t start_;
};

class contains_keyword final : public keyword
{
public:
    contains_keyword(const node* subschema, std::uint64_t minimum, std::optional<std::uint64_t> maximum)
        : subschema_(subschema), minimum_(minimum), maximum_(maximum)
    {
    }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        const bool see_all = annotations != nullptr || context.collect_all();
        if (minimum_ == 0 && !maximum_ && !see_all) {
            return true;    // always satisfied, and nothing needs its annotations
        }
        // Items that don't match are not errors, so their errors and classifications go.
        const auto mark = context.begin_conditional();
        std::uint64_t matches = 0;
        for (std::size_t i = 0; i < instance.size(); ++i) {
            const std::size_t first = context.current_mark().classifications;
            const eval_context::errors_off quiet(context);
            if (evaluate_child(*subschema_, instance[i], context, segment(name()), item_token(i))) {
                ++matches;
                if (annotations != nullptr) {
                    annotations->mark_item(i, instance.size());
                }
                if (!see_all && (maximum_ ? matches > *maximum_ : matches >= minimum_)) {
                    break;  // the outcome can no longer change
                }
            } else if (context.classifying()) {
                context.erase_classifications(first, context.current_mark().classifications);
            }
        }
        context.end_conditional(mark, false, *this, instance, {});
        if (matches < minimum_) {
            context.fail(*this, instance, [&] {
                return "the array contains " + count_text(static_cast<std::size_t>(matches), "matching item", "matching items") +
                       ", fewer than the minimum of " + std::to_string(minimum_);
            });
            return false;
        }
        if (maximum_ && matches > *maximum_) {
            context.fail(*this, instance, [&] {
                return "the array contains " + std::string(see_all ? "" : "more than ") +
                       count_text(static_cast<std::size_t>(see_all ? matches : *maximum_), "matching item", "matching items") +
                       ", more than the maximum of " + std::to_string(*maximum_);
            });
            return false;
        }
        return true;
    }

private:
    const node* subschema_;
    std::uint64_t minimum_;
    std::optional<std::uint64_t> maximum_;
};

// ---------------------------------------------------------------------------
// Unevaluated vocabulary. These run last in a node (doc 2.4) and read the node's own frame,
// which the evaluator always provides for a node that has them.
// ---------------------------------------------------------------------------

class unevaluated_properties_keyword final : public keyword
{
public:
    explicit unevaluated_properties_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_object()) {
            return true;
        }
        bool valid = true;
        std::size_t index = 0;
        const std::size_t count = instance.size();
        for (auto it = instance.begin(); it != instance.end(); ++it, ++index) {
            if (annotations->property_evaluated(index)) {
                continue;
            }
            const bool ok = subschema_->always_false
                                ? reject_member(*this, context, it.key(), it.value())
                                : evaluate_child(*subschema_, it.value(), context, segment(name()), member_token(it.key()));
            annotations->mark_property(index, count);
            if (!ok) {
                valid = false;
                if (!context.exhaustive()) {
                    return false;
                }
            }
        }
        return valid;
    }

private:
    const node* subschema_;
};

class unevaluated_items_keyword final : public keyword
{
public:
    explicit unevaluated_items_keyword(const node* subschema) : subschema_(subschema) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        if (!instance.is_array()) {
            return true;
        }
        bool valid = true;
        for (std::size_t i = 0; i < instance.size(); ++i) {
            if (annotations->item_evaluated(i)) {
                continue;
            }
            bool ok;
            if (subschema_->always_false) {
                const path_scope scope(context, item_token(i));
                context.fail(*this, instance[i], [&] { return "item " + std::to_string(i) + " is not allowed here (unevaluatedItems)"; });
                ok = false;
            } else {
                ok = evaluate_child(*subschema_, instance[i], context, segment(name()), item_token(i));
            }
            if (!ok) {
                valid = false;
                if (!context.exhaustive()) {
                    return false;
                }
            }
        }
        annotations->all_items = true;
        return valid;
    }

private:
    const node* subschema_;
};

} // namespace

std::unique_ptr<keyword> make_all_of(const compile_scope& scope)
{
    return std::make_unique<all_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_any_of(const compile_scope& scope)
{
    return std::make_unique<any_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_one_of(const compile_scope& scope)
{
    return std::make_unique<one_of_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_not(const compile_scope& scope)
{
    return std::make_unique<not_keyword>(scope.subschema(scope.value()));
}

std::unique_ptr<keyword> make_if(const compile_scope& scope)
{
    const json* then_value = scope.sibling("then");
    const json* else_value = scope.sibling("else");
    const node* condition = scope.subschema(scope.value());
    return std::make_unique<if_keyword>(condition, then_value ? scope.subschema(*then_value) : nullptr,
                                        else_value ? scope.subschema(*else_value) : nullptr);
}

std::unique_ptr<keyword> make_dependent_schemas(const compile_scope& scope)
{
    return std::make_unique<dependent_schemas_keyword>(subschema_map(scope));
}

std::unique_ptr<keyword> make_dependencies(const compile_scope& scope)
{
    if (!scope.options().legacy_dependencies) {
        return nullptr;     // strict 2020-12: an unknown keyword
    }
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas or arrays of strings");
    }
    std::vector<dependencies_keyword::dependency> dependencies;
    for (const auto& [name, value] : scope.value().items()) {
        dependencies_keyword::dependency d;
        d.name = name;
        if (value.is_array()) {
            for (const auto& element : value) {
                if (!element.is_string()) {
                    scope.fail("an array member must contain only strings");
                }
                d.required.push_back(element.get<std::string>());
            }
        } else {
            d.subschema = scope.subschema(value);
        }
        dependencies.push_back(std::move(d));
    }
    return std::make_unique<dependencies_keyword>(std::move(dependencies));
}

std::unique_ptr<keyword> make_properties(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas");
    }
    std::vector<properties_keyword::property> properties;
    for (const auto& [name, value] : scope.value().items()) {
        properties.push_back({name, scope.subschema(value), is_discriminator_schema(value)});
    }
    std::sort(properties.begin(), properties.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return std::make_unique<properties_keyword>(std::move(properties));
}

std::unique_ptr<keyword> make_pattern_properties(const compile_scope& scope)
{
    if (!scope.value().is_object()) {
        scope.fail("the value must be an object whose values are schemas");
    }
    pattern_list patterns;
    std::vector<const node*> subschemas;
    for (const auto& [pattern, value] : scope.value().items()) {
        patterns.emplace_back(pattern, scope.regex(pattern));
        subschemas.push_back(scope.subschema(value));
    }
    return std::make_unique<pattern_properties_keyword>(std::move(patterns), std::move(subschemas));
}

std::unique_ptr<keyword> make_additional_properties(const compile_scope& scope)
{
    const node* subschema = scope.subschema(scope.value());
    std::unordered_set<std::string> names;
    if (const json* properties = scope.sibling("properties"); properties != nullptr && properties->is_object()) {
        for (const auto& [name, value] : properties->items()) {
            static_cast<void>(value);
            names.insert(name);
        }
    }
    std::vector<std::shared_ptr<const compiled_regex>> patterns;
    if (const json* pattern_properties = scope.sibling("patternProperties");
        pattern_properties != nullptr && pattern_properties->is_object()) {
        const compile_scope pattern_scope = scope.for_sibling("patternProperties", *pattern_properties);
        for (const auto& [pattern, value] : pattern_properties->items()) {
            static_cast<void>(value);
            patterns.push_back(pattern_scope.regex(pattern));
        }
    }
    return std::make_unique<additional_properties_keyword>(subschema, std::move(names), std::move(patterns));
}

std::unique_ptr<keyword> make_property_names(const compile_scope& scope)
{
    return std::make_unique<property_names_keyword>(scope.subschema(scope.value()));
}

std::unique_ptr<keyword> make_prefix_items(const compile_scope& scope)
{
    return std::make_unique<prefix_items_keyword>(subschema_array(scope));
}

std::unique_ptr<keyword> make_items(const compile_scope& scope)
{
    if (scope.value().is_array()) {
        scope.fail("in 2020-12, items takes a single schema; use prefixItems for per-position schemas");
    }
    std::size_t start = 0;
    if (const json* prefix = scope.sibling("prefixItems"); prefix != nullptr && prefix->is_array()) {
        start = prefix->size();
    }
    return std::make_unique<items_keyword>(scope.subschema(scope.value()), start);
}

std::unique_ptr<keyword> make_contains(const compile_scope& scope)
{
    const node* subschema = scope.subschema(scope.value());
    const auto limit = [&](std::string_view name) -> std::optional<std::uint64_t> {
        const json* value = scope.sibling(name);
        if (value == nullptr || !scope.vocabulary_enabled(vocab_validation)) {   // min/maxContains are validation keywords
            return std::nullopt;
        }
        return non_negative_integer(scope.for_sibling(name, *value));
    };
    const std::uint64_t minimum = limit("minContains").value_or(1);
    const std::optional<std::uint64_t> maximum = limit("maxContains");
    return std::make_unique<contains_keyword>(subschema, minimum, maximum);
}

std::unique_ptr<keyword> make_unevaluated_properties(const compile_scope& scope)
{
    return std::make_unique<unevaluated_properties_keyword>(scope.subschema(scope.value()));
}

std::unique_ptr<keyword> make_unevaluated_items(const compile_scope& scope)
{
    return std::make_unique<unevaluated_items_keyword>(scope.subschema(scope.value()));
}

} // namespace jsonschema::detail
