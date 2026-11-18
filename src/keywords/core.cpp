// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// Core vocabulary: `$ref` and `$dynamicRef` (design doc 2.6).
#include <algorithm>

#include "keywords/keywords.hpp"

namespace jsonschema::detail {

namespace {

class ref_keyword final : public keyword
{
public:
    explicit ref_keyword(const node* target) : target_(target) {}

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        return evaluate_in_place(*target_, instance, context, annotations, segment("$ref"), edge::ref);
    }

    void in_place_subschemas(std::vector<const node*>& out) const override { out.push_back(target_); }
    void collect_facts(node_facts& facts) const override { facts.always_applied.push_back(target_); }

private:
    const node* target_;
};

class dynamic_ref_keyword final : public keyword
{
public:
    explicit dynamic_ref_keyword(const node* initial) : initial_(initial) {}

    dynamic_anchor_binding& binding() noexcept { return binding_; }

    [[nodiscard]] bool evaluate(const json& instance, eval_context& context, frame* annotations) const override
    {
        const node* target = initial_;
        if (!binding_.anchor.empty()) {
            // The outermost resource in the dynamic scope that defines the anchor wins.
            for (const schema_resource* resource : context.dynamic_scope()) {
                const auto it = std::find_if(binding_.candidates.begin(), binding_.candidates.end(),
                                             [&](const auto& candidate) { return candidate.first == resource; });
                if (it != binding_.candidates.end()) {
                    target = it->second;
                    break;
                }
            }
        }

        // The target depends on the dynamic scope, so cycles cannot all be found at compile
        // time. Re-entering this keyword at the same instance location is one.
        auto& stack = context.dynamic_ref_stack;
        for (auto it = stack.rbegin(); it != stack.rend() && it->instance == &instance; ++it) {
            if (it->reference == this) {
                throw evaluation_error("json-schema-cpp: infinite recursion through $dynamicRef to " + describe(*target));
            }
        }
        stack.push_back({this, &instance});
        const bool valid = evaluate_in_place(*target, instance, context, annotations, segment("$dynamicRef"), edge::ref);
        stack.pop_back();
        return valid;
    }
    void collect_facts(node_facts& facts) const override { facts.always_applied.push_back(initial_); }

private:
    const node* initial_;
    dynamic_anchor_binding binding_;
};

std::string_view reference_text(const compile_scope& scope)
{
    if (!scope.value().is_string()) {
        scope.fail("the value must be a string");
    }
    return scope.value().get_ref<const std::string&>();
}

} // namespace

std::unique_ptr<keyword> make_ref(const compile_scope& scope)
{
    return std::make_unique<ref_keyword>(scope.reference(reference_text(scope)).target_node);
}

std::unique_ptr<keyword> make_dynamic_ref(const compile_scope& scope)
{
    const auto resolved = scope.reference(reference_text(scope));
    auto result = std::make_unique<dynamic_ref_keyword>(resolved.target_node);

    // Dynamic resolution applies only when the initial target is a `$dynamicAnchor` with
    // the fragment's name (the 2020-12 "bookend" rule). Otherwise this is a plain `$ref`.
    const auto& fragment = resolved.target.fragment();
    if (fragment && !fragment->empty() && fragment->front() != '/') {
        const auto name = percent_decode(*fragment);
        const schema_resource* resource = scope.schemas().location_of(*resolved.value).resource;
        if (name && resource != nullptr) {
            const auto anchor = resource->dynamic_anchors.find(*name);
            if (anchor != resource->dynamic_anchors.end() && anchor->second == resolved.value) {
                result->binding().anchor = *name;
                scope.bind_dynamic_anchor(result->binding());
            }
        }
    }
    return result;
}

} // namespace jsonschema::detail
