// SPDX-FileCopyrightText: 2026 Robert Patterson
// SPDX-License-Identifier: MIT
//
// The compiled schema graph (design doc 2.4) and the evaluator's per-call state (doc 2.5).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <jsonschema/classification.hpp>
#include <jsonschema/validation.hpp>

namespace jsonschema::detail {

using json = nlohmann::json;

struct schema_resource;
struct node;
class eval_context;
struct frame;

/// Facts a keyword contributes to a named node's `schema_info` (doc 4.5).
struct node_facts
{
    std::vector<std::string_view> declared_properties;
    std::vector<const node*> always_applied;    ///< subschemas applied unconditionally at the same location
};

/// One compiled keyword (or a group evaluated together, such as `contains` with
/// `minContains`/`maxContains`).
class keyword
{
public:
    keyword() = default;
    keyword(const keyword&) = delete;
    keyword& operator=(const keyword&) = delete;
    virtual ~keyword() = default;

    /// True if @p instance satisfies the keyword. Keywords that do not apply to the instance's
    /// type return true. @p annotations is the frame for this instance location, or nullptr if
    /// nothing needs annotations here (doc 2.5).
    [[nodiscard]] virtual bool evaluate(const json& instance, eval_context& context, frame* annotations) const = 0;

    /// Appends the subschemas this keyword applies to the same instance location. Used to
    /// reject reference cycles that never consume input.
    virtual void in_place_subschemas(std::vector<const node*>& /*out*/) const {}

    /// Contributes to `schema_info` for classification.
    virtual void collect_facts(node_facts& /*facts*/) const {}

    /// Set by the compiler: the keyword's name (as written in the schema) and the absolute
    /// keyword location, both used in errors.
    void bind(std::string_view name, std::string location)
    {
        name_ = name;
        location_ = std::move(location);
    }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] const std::string& location() const noexcept { return location_; }

private:
    std::string_view name_;
    std::string location_;
};

/// One compiled schema location.
struct node
{
    const json* schema = nullptr;               ///< the schema value (object or boolean)
    const schema_resource* resource = nullptr;  ///< innermost enclosing resource
    std::string pointer;                        ///< JSON pointer from the resource root
    std::string location;                       ///< absolute URI: resource base + "#" + encoded pointer
    bool always_false = false;                  ///< the `false` schema
    bool has_unevaluated = false;               ///< has `unevaluatedProperties`/`unevaluatedItems`: needs its own frame
    std::vector<std::unique_ptr<keyword>> keywords;   ///< in evaluation order (doc 2.4)
    /// Annotation keywords (`title`, unknown keywords, ...) reported to an annotation sink.
    std::vector<std::pair<std::string_view, const json*>> annotations;

    /// Classification (doc 4); set only when classification is enabled.
    std::string name;                           ///< empty: not a named schema
    std::unique_ptr<schema_info> info;          ///< for named nodes
};

/// Annotations for one instance location that `unevaluated*` consumes (doc 2.5).
struct frame
{
    std::vector<bool> properties;   ///< evaluated object members, by index in iteration order
    std::size_t item_prefix = 0;    ///< items [0, item_prefix) were evaluated
    bool all_items = false;
    std::vector<bool> items;        ///< individually evaluated items (`contains`)

    void mark_property(std::size_t index, std::size_t count)
    {
        if (properties.size() < count) {
            properties.resize(count);
        }
        properties[index] = true;
    }
    [[nodiscard]] bool property_evaluated(std::size_t index) const noexcept
    {
        return index < properties.size() && properties[index];
    }
    void mark_prefix(std::size_t count) noexcept { item_prefix = count > item_prefix ? count : item_prefix; }
    void mark_item(std::size_t index, std::size_t count)
    {
        if (items.size() < count) {
            items.resize(count);
        }
        items[index] = true;
    }
    [[nodiscard]] bool item_evaluated(std::size_t index) const noexcept
    {
        return all_items || index < item_prefix || (index < items.size() && items[index]);
    }
    void merge(const frame& other)
    {
        if (properties.size() < other.properties.size()) {
            properties.resize(other.properties.size());
        }
        for (std::size_t i = 0; i < other.properties.size(); ++i) {
            if (other.properties[i]) {
                properties[i] = true;
            }
        }
        mark_prefix(other.item_prefix);
        all_items = all_items || other.all_items;
        if (items.size() < other.items.size()) {
            items.resize(other.items.size());
        }
        for (std::size_t i = 0; i < other.items.size(); ++i) {
            if (other.items[i]) {
                items[i] = true;
            }
        }
    }
};

/// How evaluation entered a node (doc 4.3 `via`). Ordered so that combining edges with `max`
/// gives the strongest one since the previous match.
enum class edge : std::uint8_t
{
    none,
    root,
    child,
    ref,
    in_place
};

/// One step of the evaluation path: a keyword, optionally with a member name or index.
struct path_segment
{
    std::string_view keyword;
    std::string_view key;
    std::size_t index = 0;
    enum class kind : std::uint8_t { keyword_only, with_key, with_index } shape = kind::keyword_only;
};

/// One step of the instance location.
struct instance_token
{
    std::string_view key;
    std::size_t index = 0;
    bool is_index = false;
};

/// One subschema evaluation on the way to an error or annotation, for hierarchical
/// output (2020-12 §12.4.3 "detailed").
struct output_unit
{
    std::string keyword_location;       ///< evaluation path to the subschema
    std::string_view absolute_location; ///< the subschema's absolute location
    std::string instance_location;
};

/// Per-validation state. Not shared between threads.
class eval_context
{
public:
    struct error_entry
    {
        json::json_pointer instance_location;
        std::string evaluation_path;
        std::string_view schema_location;
        std::string_view keyword;
        std::string message;
        const json* instance;
        std::vector<output_unit> chain;   ///< enclosing subschema evaluations, outermost first (if recorded)
    };

    struct settings
    {
        bool track_dynamic_scope = false;
        error_sink* errors = nullptr;
        std::size_t max_errors = 0;
        bool annotations = false;
        bool classify = false;
        bool recover = false;
        bool record_chains = false;                         ///< for "detailed" output
        std::vector<error_entry>* captured_errors = nullptr;   ///< delivered errors are also copied here
    };
    explicit eval_context(const settings& s);

    // --- dynamic scope (doc 2.6) -------------------------------------------------------
    [[nodiscard]] const std::vector<const schema_resource*>& dynamic_scope() const noexcept { return dynamic_scope_; }
    struct dynamic_ref_frame
    {
        const keyword* reference;
        const json* instance;
    };
    std::vector<dynamic_ref_frame> dynamic_ref_stack;
    bool enter_resource(const schema_resource* resource)
    {
        if (!settings_.track_dynamic_scope || (!dynamic_scope_.empty() && dynamic_scope_.back() == resource)) {
            return false;
        }
        dynamic_scope_.push_back(resource);
        return true;
    }
    void leave_resource() noexcept { dynamic_scope_.pop_back(); }

    // --- modes ---------------------------------------------------------------------------
    [[nodiscard]] bool reporting_errors() const noexcept { return settings_.errors != nullptr; }
    /// Paths are tracked only when something will report them.
    [[nodiscard]] bool tracking_paths() const noexcept { return settings_.errors != nullptr || settings_.annotations; }
    [[nodiscard]] bool classifying() const noexcept { return settings_.classify; }
    /// Keep evaluating after a failure (to report every error, or to classify everything).
    [[nodiscard]] bool exhaustive() const noexcept { return (settings_.errors != nullptr && !error_limit_reached_) || settings_.classify; }
    /// Evaluate every branch of `anyOf` and every item of `contains`, so that all passing
    /// branches contribute annotations and classifications.
    [[nodiscard]] bool collect_all() const noexcept { return settings_.annotations || settings_.classify; }

    // --- paths ---------------------------------------------------------------------------
    std::vector<path_segment> evaluation_path;
    std::vector<instance_token> instance_path;
    [[nodiscard]] json::json_pointer instance_pointer() const;
    [[nodiscard]] std::string evaluation_path_text(std::string_view last_keyword) const;

    // --- errors --------------------------------------------------------------------------
    /// Reports that @p source failed on @p instance. @p message is called only if errors are reported.
    template <typename MessageFn>
    void fail(const keyword& source, const json& instance, MessageFn&& message)
    {
        if (settings_.errors != nullptr) {
            add_error(source.name(), source.location(), instance, std::string(message()));
        }
    }
    void add_error(std::string_view keyword_name, std::string_view schema_location, const json& instance, std::string message);

    /// A conditional applicator (`anyOf`, `oneOf`, `not`, `if`, `contains`) brackets the
    /// evaluation whose errors it may discard.
    struct mark
    {
        std::size_t errors;
        std::size_t classifications;
        std::size_t annotations;
    };
    mark begin_conditional();
    /// Ends a conditional. If @p keep_errors, the bracketed errors stand, preceded by the summary
    /// error (if @p summary is not empty); otherwise they are discarded.
    void end_conditional(const mark& m, bool keep_errors, const keyword& source, const json& instance, std::string summary);
    [[nodiscard]] mark current_mark() const noexcept { return {errors_.size(), classifications_.size(), annotations_.size()}; }

    // --- annotations and classification logs ----------------------------------------------
    struct annotation_entry
    {
        json::json_pointer instance_location;
        std::string evaluation_path;
        std::string schema_location;
        std::string_view keyword;
        const json* value;
        std::vector<output_unit> chain;   ///< as for errors
    };
    struct classification_entry
    {
        const json* instance;
        const node* named;
        edge reached_by;
        jsonschema::confidence confidence;
    };
    std::vector<annotation_entry> annotations_;
    std::vector<classification_entry> classifications_;
    void truncate_classifications(std::size_t size) { classifications_.resize(size); }
    void truncate_annotations(std::size_t size) { annotations_.resize(size); }
    /// Removes the classification entries in [first, last).
    void erase_classifications(std::size_t first, std::size_t last)
    {
        classifications_.erase(classifications_.begin() + static_cast<std::ptrdiff_t>(first),
                               classifications_.begin() + static_cast<std::ptrdiff_t>(last));
    }
    /// Marks entries in [first, last) located at @p instance as ambiguous.
    void mark_ambiguous(std::size_t first, std::size_t last, const json& instance)
    {
        for (std::size_t i = first; i < last; ++i) {
            if (classifications_[i].instance == &instance) {
                classifications_[i].confidence = confidence::ambiguous;
            }
        }
    }

    // --- recovery (doc 4.6) ----------------------------------------------------------------
    /// Records that the node being evaluated failed in a way that means "not this kind of thing".
    void identity_failure() noexcept { identity_ = true; }
    /// After evaluating a node that failed: whether the failure was an identity failure.
    [[nodiscard]] bool last_failure_was_identity() const noexcept { return last_identity_; }
    [[nodiscard]] bool recovering() const noexcept { return settings_.recover; }

    /// The message of the first error buffered since @p m, if any.
    [[nodiscard]] std::string first_error_since(const mark& m) const
    {
        return m.errors < errors_.size() ? errors_[m.errors].message : std::string();
    }

    /// Delivers buffered errors once no conditional is open.
    void flush_errors();

    /// Turns error reporting off for its lifetime. Conditional applicators evaluate their
    /// branches this way and re-run failed branches with errors only if the errors will be
    /// delivered, so valid documents never build error text that is thrown away.
    class errors_off
    {
    public:
        explicit errors_off(eval_context& context) : context_(context), saved_(context.settings_.errors)
        {
            context_.settings_.errors = nullptr;
        }
        errors_off(const errors_off&) = delete;
        errors_off& operator=(const errors_off&) = delete;
        ~errors_off() { context_.settings_.errors = saved_; }

    private:
        eval_context& context_;
        error_sink* saved_;
    };

private:
    friend bool evaluate(const node&, const json&, eval_context&, frame*, edge);

    void deliver(const error_entry& e);

    /// The subschema evaluations currently open, for `record_chains`.
    struct chain_entry
    {
        const node* subschema;
        std::size_t evaluation_path_length;
        std::size_t instance_path_length;
    };
    std::vector<chain_entry> chain_;
    [[nodiscard]] std::vector<output_unit> current_chain() const;

    settings settings_;
    std::vector<const schema_resource*> dynamic_scope_;
    std::vector<error_entry> errors_;
    std::size_t open_conditionals_ = 0;
    std::size_t delivered_errors_ = 0;
    bool error_limit_reached_ = false;
    bool identity_ = false;
    bool last_identity_ = false;
    edge via_ = edge::none;
};

/// Evaluates @p instance against @p schema_node, entered through @p entered_by.
[[nodiscard]] bool evaluate(const node& schema_node, const json& instance, eval_context& context, frame* annotations,
                            edge entered_by);

// --- helpers for keywords ---------------------------------------------------------------------

/// Pushes an evaluation-path segment (and optionally an instance token) while tracking paths.
class path_scope
{
public:
    path_scope(eval_context& context, const path_segment& segment) : context_(context), push_(context.tracking_paths())
    {
        if (push_) {
            context_.evaluation_path.push_back(segment);
        }
    }
    path_scope(eval_context& context, const path_segment& segment, const instance_token& token)
        : context_(context), push_(context.tracking_paths()), token_(true)
    {
        if (push_) {
            context_.evaluation_path.push_back(segment);
            context_.instance_path.push_back(token);
        }
    }
    /// Pushes only an instance token: for errors a keyword reports about one of its members,
    /// whose evaluation path is the keyword itself.
    path_scope(eval_context& context, const instance_token& token) : context_(context), push_(context.tracking_paths()), segment_(false), token_(true)
    {
        if (push_) {
            context_.instance_path.push_back(token);
        }
    }
    path_scope(const path_scope&) = delete;
    path_scope& operator=(const path_scope&) = delete;
    ~path_scope()
    {
        if (push_) {
            if (segment_) {
                context_.evaluation_path.pop_back();
            }
            if (token_) {
                context_.instance_path.pop_back();
            }
        }
    }

private:
    eval_context& context_;
    bool push_;
    bool segment_ = true;
    bool token_ = false;
};

inline path_segment segment(std::string_view keyword_name) { return {keyword_name, {}, 0, path_segment::kind::keyword_only}; }
inline path_segment segment(std::string_view keyword_name, std::string_view key)
{
    return {keyword_name, key, 0, path_segment::kind::with_key};
}
inline path_segment segment(std::string_view keyword_name, std::size_t index)
{
    return {keyword_name, {}, index, path_segment::kind::with_index};
}
inline instance_token member_token(std::string_view key) { return {key, 0, false}; }
inline instance_token item_token(std::size_t index) { return {{}, index, true}; }

/// Applies @p sub to a child of the current instance (a member or an item).
inline bool evaluate_child(const node& sub, const json& value, eval_context& context, const path_segment& seg,
                           const instance_token& token)
{
    const path_scope scope(context, seg, token);
    return evaluate(sub, value, context, nullptr, edge::child);
}

/// Applies @p sub to the current instance. Propagates an identity failure of @p sub to the
/// node being evaluated, for applicators whose failure is the node's failure.
inline bool evaluate_in_place(const node& sub, const json& instance, eval_context& context, frame* annotations,
                              const path_segment& seg, edge entered_by)
{
    const path_scope scope(context, seg);
    const bool valid = evaluate(sub, instance, context, annotations, entered_by);
    if (!valid && context.last_failure_was_identity()) {
        context.identity_failure();
    }
    return valid;
}

/// Percent-encodes a JSON pointer for use as a URI fragment.
[[nodiscard]] std::string encode_fragment(std::string_view pointer);

/// The JSON Schema type name of an instance ("integer" for whole numbers).
[[nodiscard]] std::string_view type_name(const json& instance);

/// A short JSON rendering of @p value for messages (truncated).
[[nodiscard]] std::string brief(const json& value);

} // namespace jsonschema::detail
