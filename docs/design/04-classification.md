# 4. Schema classification

## 4.1 Motivation

Applications often need to know *what kind of thing* each part of a document is, in
the schema's own terms. The motivating case is mnxdom's `EntityMap`. It indexes every
object carrying an `id` and records the `$defs` name of the schema definition that
describes it (`event`, `note`, `measure`, ...).

mnxdom does this today with its own walker (`mnxdom/src/util/IdIndexer.cpp`), separate
from validation. That walker re-implements part of a validator:
- following `$ref` into `$defs`,
- choosing an `anyOf`/`oneOf` branch by matching the instance's `type` property to a
  branch's `const`, or a branch that doesn't require `type`,
- searching `properties`, then `allOf` members, then `patternProperties`, then
  `additionalProperties` for each member,
- telling a named property apart from a dictionary entry that happens to be called `id`.

That gives two passes over every document, and a second, less complete interpretation
of the schema that can drift from the validator's. For example, the walker's branch
selection is a heuristic, while the validator *knows* which `anyOf` branch passed.

During validation, the evaluator already learns exactly which subschema matched each
instance location. Classification makes that available as an annotation-like output.

## 4.2 Concepts

- **Named schema.** A schema node the compiler has given a name. The naming rule is
  configurable (4.4). The default names every direct child of a `$defs` keyword with
  its key.
- **Classification.** A record that an instance location was successfully evaluated
  by one or more named schemas.

Classifications follow the **same retention rules as annotations** (doc 2.5): they are
kept only if every subschema between them and the root passed. A failing `anyOf`
branch therefore never classifies anything, and the branch that passed classifies
exactly what it matched. That is the precision the current heuristic lacks.

This strict rule applies to documents that pass. For documents that fail, a recovery
policy relaxes it in a controlled way (4.6), so that one error doesn't wipe out the
classification of everything around it.

## 4.3 API

```cpp
struct classification {
    json::json_pointer           instance_location;
    const json&                  instance;     // valid during the callback
    std::span<const match>       matches;      // outermost first; empty only in complete-walk mode (4.6)
    classification_status        status;       // summary of the matches (4.6)
};

struct match {
    std::string_view   name;               // e.g. "event"
    std::string_view   schema_location;    // absolute keyword location of the named node
    via                reached_by;         // how evaluation entered this node, relative to the previous match
    confidence         confidence;         // exact | recovered | ambiguous (4.6)
    const schema_info& info;               // compile-time facts about the node (4.5)
};

enum class via {
    root,            // the root schema itself is named
    child,           // reached from the parent instance via properties/items/additionalProperties/...
    ref,             // reached by $ref / $dynamicRef from a node at the same instance location
    in_place,        // reached through allOf/anyOf/oneOf/if/then/else/dependentSchemas
};

class classification_sink {
public:
    virtual ~classification_sink() = default;
    virtual void on_classified(const classification&) = 0;
};

struct classification_options {
    bool            enabled = false;                    // compile-time switch; off = zero cost
    namer           name_rule = namer::defs_children(); // see 4.4
    recovery_policy recovery = recovery_policy::discriminators();  // see 4.6; recovery_policy::none() = strict
    bool            complete_walk = true;               // see 4.6
};
```

**One callback per instance location.** All the named schemas that applied there are
delivered together, in evaluation order from the outside in. For an MNX `accent`
object, `matches` would be

```
[ {name:"accent",       reached_by: ref},
  {name:"global-attrs", reached_by: in_place} ]   // via allOf → $ref
```

That lets each consumer choose its own policy, rather than the library guessing.
mnxdom's current policy, "the name reached through the `$ref` chain, ignoring names
reached through `allOf`", becomes: *take the last match before the first `in_place`*.

**`via`** is the strongest edge since the previous match at the same location, where
`in_place` beats `ref` and `ref` beats `child`/`root`. An item reached through
`items → anyOf → $ref` is therefore `in_place`.

**Delivery order.** Callbacks arrive in document order, after validation finishes. A
location's matches aren't final until every enclosing applicator has decided. Until
then they are kept in a classification log that is cut back at the same marks as
annotations (doc 2.5). At the end, a structural walk of the instance delivers each
location's matches by instance address. Memory is proportional to the number of
matches, and it is still one pass of validation.

## 4.4 Naming rules

Names are assigned **at compile time**. Evaluation only checks a flag on the node, so
unnamed nodes cost nothing.

```cpp
class namer {
public:
    static namer defs_children();                 // "$defs/<key>" → "<key>"  (default)
    static namer anchors();                       // "$anchor" value
    static namer custom(std::function<std::optional<std::string>(const schema_node_view&)>);
    namer operator|(namer other) const;           // first rule that produces a name wins
};
```

`schema_node_view` exposes the node's absolute location, its JSON, and its parent
keyword. A custom rule could, for example, name nodes by a vendor keyword
(`"x-class": "Event"`), which maps directly onto C++ class names.

## 4.5 Schema facts for consumers

mnxdom has to tell `{"id": "m1"}` on a `measure` (an id) apart from `"id"` used as a
dictionary key (in `kit` or `sounds`, it's not an id). The validator already knows
which keyword covered each member, so `schema_info` exposes it:

```cpp
class schema_info {
public:
    bool declares_property(std::string_view name) const;   // listed in this node's `properties`, or in those
                                                           // of a schema it always applies at the same location
};
```

"Always applies" means through `$ref`, `$dynamicRef` (its initial target) and `allOf`.
It is decided at compile time, so the check costs one binary search. Branches of
`anyOf`/`oneOf` are reported as matches of their own, and a consumer can ask their
`info` instead.

So the mnxdom check becomes: `instance` is an object, `instance["id"]` is a string, and
`matches.back().info.declares_property("id")` or the policy-selected match's does.
No second walk is needed.

## 4.6 Invalid documents: recovery and complete walks

Goal: with classification on, a single `validate` call replaces mnxdom's own walk
entirely, **including on documents that fail validation**. Errors are reported in
full, and every recoverable part of the document is still classified.

Under strict annotation rules a single error destroys too much. A `note` missing a
required property fails. So does its `event`, its `sequence`, and the `anyOf` that
chose between `event`, `grace` and `tuplet`, which removes every classification above
and below the error. Two mechanisms prevent this.

### Identity errors vs content errors

When a named node fails, the evaluator asks the recovery policy whether the failure
means **"this is not that kind of thing"** (an identity error) or **"this is that kind
of thing, but it's wrong"** (a content error).

The default policy, `recovery_policy::discriminators()`, treats these as identity
errors:

1. `type` failing at the node's own instance location (an array where an object was
   expected);
2. `const` or `enum` failing at the node's own instance location;
3. a failure on a **discriminator property**: a property whose subschema in this
   node is a `const` or single-value `enum`, such as MNX's `"type": {"const": "grace"}`.
   The property is missing when `required`, or present with the wrong value.

Discriminator properties are found at compile time, so the check costs nothing extra
during validation. Everything else is a content error: other `required` properties,
`unevaluatedProperties`/`additionalProperties`, numeric and string limits, `pattern`,
`format`, and any error deeper in the instance.

A named node whose failures are **all content errors** keeps its classification, with
`confidence::recovered`. Its errors are still reported to the error sink normally.

### Choosing among failed branches

When every branch of an `anyOf`/`oneOf` fails, the evaluator applies the same test to
each branch:

| Branches with no identity errors | Result |
|---|---|
| exactly one | that branch's classifications are kept, `confidence::recovered` |
| more than one | all candidates reported, `confidence::ambiguous` |
| none | the subtree is unclassified (but still walked, see below) |

`oneOf` with more than one *passing* branch is an error, and is classified as
`ambiguous` among the passing branches. `not` and a failing `if` never contribute
classifications, which matches the annotation rules.

`recovery_policy::none()` turns all of this off: classifications follow the strict
annotation rules.

`recovery_policy::custom(fn)`, which would let a consumer decide per failed node from
its error list, is **not implemented**. It would need error lists collected even
without an error sink. The default suits any schema that uses `const` discriminators,
which is the common JSON Schema idiom, and nothing so far needs more.

**How identity propagates.** An identity failure in a subschema applied
unconditionally at the same location (`$ref`, `allOf`, `then`/`else`) is an identity
failure of the node that applied it. That is how an `anyOf` branch that is only
`{"$ref": "#/$defs/grace"}` counts as "not a grace". A failure inside a child
location is a content failure of the parent, except for a discriminator property.
Discriminators are found in every node, named or not.

### Complete walk

`complete_walk = true` guarantees that **every object and array in the instance gets
exactly one callback**, in document order, even where no schema applied: members
rejected by `additionalProperties: false`/`unevaluatedProperties: false`, members of
unclassified subtrees, and members with no schema. Those callbacks have empty `matches`
and `status == unclassified`.

The evaluator already walks everything it validates. For the parts no schema covers,
it does a plain structural walk with no validation work.

### Summary status

```cpp
enum class confidence             { exact, recovered, ambiguous };
enum class classification_status  { exact, recovered, ambiguous, unclassified };   // weakest match wins
```

- `exact` means the named schema itself passed and was chosen by passing, not by
  recovery.
- `recovered` means the named schema failed with content errors only, so there is
  always an error under that location.

So in an invalid document, an object far from any error is still `exact`, even though
its ancestors failed. On a valid document every match is `exact`, which is the answer
the strict annotation rules give.

### Interaction with errors

- Recovery never hides errors. Every error is delivered to the error sink as usual,
  and a recovered match is always accompanied by at least one error under its
  instance location.
- `max_errors` only limits how many errors are *delivered*. When a classification
  sink is present, the walk continues to the end.
- The one-pass guarantee still holds: errors and classifications come from the same
  evaluation.

### What this means for mnxdom

- **A valid document:** every id is classified `exact`. Same result as today, but
  derived from validation rather than a heuristic.
- **A document with content errors** (a stray property, a missing non-discriminator
  property, an out-of-range value): ids are classified `recovered` with the right
  type name.
- **A document with an unknown or misspelled `type` discriminator:** that object is
  `unclassified`, and mnxdom stores an empty type name, as it does today. Its children
  are still walked, and they are classified wherever their own schemas can be resolved
  without the parent: for example, everything reached through a `$ref` property that
  doesn't depend on the parent's branch.

## 4.7 What mnxdom would look like

```cpp
auto schema = jsonschema::schema::compile(getMnxSchemaJson(), {
    .classification = { .enabled = true },
});

struct IdCollector : jsonschema::classification_sink {
    EntityMap& map;
    void on_classified(const jsonschema::classification& c) override {
        if (!c.instance.is_object()) return;
        auto id = c.instance.find("id");
        if (id == c.instance.end() || !id->is_string()) return;
        if (c.status == jsonschema::classification_status::unclassified) {
            map.add(id->get<std::string>(), c.instance_location, "");   // as today: unknown type
            return;
        }
        const auto& m = mnxPolicy(c.matches);            // last match before first in_place
        if (!m.info.declares_property("id")) return;     // dictionary key, not an id
        map.add(id->get<std::string>(), c.instance_location,
                c.status == jsonschema::classification_status::ambiguous ? "" : std::string(m.name));
    }
};

collecting_error_sink errors;
IdCollector ids{map};
schema.validate(doc, { .errors = &errors, .classifications = &ids });   // one pass
```

`IdIndexer.cpp` goes away, and so does mnxdom's hard-coded knowledge of the schema
layout (`#/$defs/` prefixes, `type`-based branch selection). Skipping vendor `_x` and
comment `_c` subtrees remains a choice mnxdom makes, by checking `instance_location`.

In complete-walk mode, the dictionary-key case ("`id`" used as a key in `kit` or
`sounds`) is still handled by `declares_property`. The callback for the `kit` object
is classified as `kit`, and `kit` doesn't declare an `id` property.

## 4.8 Cost

- Compile time: one pass to assign names. Nodes are flagged so evaluation can tell.
- Validation with the feature disabled at compile time: zero. The code path is not taken.
- Enabled at compile time but no sink passed to `validate`: one branch per named node.
- Enabled with a sink: annotation frames are kept for named nodes, as for
  `unevaluated*`. The MNX schema already needs frames almost everywhere for
  `unevaluatedProperties`, so the extra cost there is small.
- Recovery: no cost on documents that pass. On failures, each failed named node
  checks its own errors against its compile-time discriminator list, which is cheap.
  Failed branches keep their errors and classifications until the enclosing
  applicator decides. That is already required to report errors correctly.
- Complete walk: only the parts no schema covers get the extra structural walk.
