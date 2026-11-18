// Copyright (c) 2026 Robert Patterson. MIT License; see LICENSE.
//
// The compiled schema graph (design doc 2.4) and the evaluator entry point (doc 2.5).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace jsonschema::detail {

using json = nlohmann::json;

struct schema_resource;
struct node;
class eval_context;

/// One compiled keyword (or a group of keywords that are evaluated together, such as
/// `contains` with `minContains`/`maxContains`).
class keyword
{
public:
    keyword() = default;
    keyword(const keyword&) = delete;
    keyword& operator=(const keyword&) = delete;
    virtual ~keyword() = default;

    /// True if @p instance satisfies the keyword. Keywords that do not apply to the
    /// instance's type return true.
    [[nodiscard]] virtual bool evaluate(const json& instance, eval_context& context) const = 0;

    /// Appends the subschemas this keyword applies to the same instance location
    /// (`$ref`, `allOf`, ...). Used to reject reference cycles that never consume input.
    virtual void in_place_subschemas(std::vector<const node*>& /*out*/) const {}
};

/// One compiled schema location.
struct node
{
    const json* schema = nullptr;               ///< the schema value (object or boolean)
    const schema_resource* resource = nullptr;  ///< innermost enclosing resource
    std::string pointer;                        ///< JSON pointer from the resource root
    bool always_false = false;                  ///< the `false` schema
    std::vector<std::unique_ptr<keyword>> keywords;   ///< in evaluation order (doc 2.4)
};

/// Per-validation state. Not shared between threads.
class eval_context
{
public:
    explicit eval_context(bool track_dynamic_scope) : track_dynamic_scope_(track_dynamic_scope) {}

    /// The resources entered on the way to the current node, outermost first (doc 2.6).
    [[nodiscard]] const std::vector<const schema_resource*>& dynamic_scope() const noexcept { return dynamic_scope_; }

    /// The `$dynamicRef` evaluations in progress, for infinite-recursion detection.
    struct dynamic_ref_frame
    {
        const keyword* reference;
        const json* instance;
    };
    std::vector<dynamic_ref_frame> dynamic_ref_stack;

    /// Records entry into @p resource. Returns true if the caller must call `leave_resource`.
    bool enter_resource(const schema_resource* resource)
    {
        if (!track_dynamic_scope_ || (!dynamic_scope_.empty() && dynamic_scope_.back() == resource)) {
            return false;
        }
        dynamic_scope_.push_back(resource);
        return true;
    }
    void leave_resource() noexcept { dynamic_scope_.pop_back(); }

private:
    bool track_dynamic_scope_;
    std::vector<const schema_resource*> dynamic_scope_;
};

/// Evaluates @p instance against @p schema_node.
[[nodiscard]] inline bool evaluate(const node& schema_node, const json& instance, eval_context& context)
{
    if (schema_node.always_false) {
        return false;
    }
    const bool entered = context.enter_resource(schema_node.resource);
    bool valid = true;
    for (const auto& k : schema_node.keywords) {
        if (!k->evaluate(instance, context)) {
            valid = false;
            break;
        }
    }
    if (entered) {
        context.leave_resource();
    }
    return valid;
}

} // namespace jsonschema::detail
