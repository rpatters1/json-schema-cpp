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
    const json::json_pointer&    instance_location;
    const json&                  instance;     // valid during the callback
    std::span<const match>       matches;      // one named match per visit
    classification_status        status;       // status of this visit (4.6)
    classification_branch_id branch;          // zero if unconditional
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
    virtual void on_branch(const classification_branch&) {}
    virtual void on_classified(const classification&) = 0;
};

struct classification_options {
    bool            enabled = false;                    // compile-time switch; off = zero cost
    namer           name_rule = namer::defs_children(); // see 4.4
    recovery_policy recovery = recovery_policy::discriminators();  // see 4.6; recovery_policy::none() = strict
};
```

**One callback per named-schema visit.** When a named node finishes, its local
outcome and the path to its instance are available. Different named schemas, or
repeated references to the same schema, can report the same instance location.
The event's pointer, instance and match are borrowed for the callback.

**Branch lifecycle.** A local match is delivered immediately. A zero branch ID
means the match can be used immediately. A nonzero ID names an open decision.
The evaluator announces `begin(id, parent)` before any event on that branch,
then sends exactly one `commit` or `discard` on normal completion. A commit
merges into its parent; a commit to zero is final. A discard drops everything
merged into the branch, including previously committed children. Sibling
alternatives can remain open together, so use IDs rather than a stack of callbacks.

For example, if an inner alternative succeeds inside an outer alternative that
later loses, the consumer sees the inner commit to the outer ID and then the outer
discard. No match is replayed. On an ambiguous commit, `ambiguous_at` identifies
the exact instance location whose pending matches become ambiguous; descendant
locations keep their own confidence.

IDs are scoped to a validation call. Callback data is borrowed for the callback.
If validation throws, abandon the call's pending decisions. A sink counting local
visits can ignore branch notifications. A sink counting accepted matches needs to
consume them. Internal diagnostic replays, negated schemas, and synthetic values
such as property names produce no match events.

**Schema edges.** `via` records the strongest edge since the enclosing named match at
this location. In-place applicators take precedence over references.

**Alternative checking.** By default classification can stop at the first passing
`anyOf` branch or once `contains` has determined its outcome. Set
`validation_options::check_all_matches = true` to collect all matches. Both modes
check `oneOf` exclusivity and collect any coverage required by `unevaluated*`.
An annotation sink independently requests all passing annotations.

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

An MNX consumer can check the reported class and use its `info.declares_property("id")`
when the instance contains a string ID.
No second walk is needed.

## 4.6 Invalid documents: recovery

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
| none | no candidate contributes a match |

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
- **An unknown discriminator:** branches ruled out by the discriminator contribute
  no named matches. The consumer does not receive placeholder callbacks for unknown IDs.

## 4.7 Consuming the stream in mnxdom

The sink can check each local match for an MNX entity class and a declared string
`id`. For an unconditional match, insert the ID directly. For a match on an open
branch, copy only the candidate ID, type, and location into that branch's pending
list. On discard, erase the list. On commit, apply any ambiguity at the specified
location and move the candidates to the parent, or to the index if the parent is
zero. The consumer can ignore vendor extensions and dictionary keys before
allocating an ID candidate.

## 4.8 Cost

Schema naming happens at compilation. Validation without a sink skips match
delivery. With a sink, the evaluator borrows the current instance pointer and
sends one match per named visit. It allocates IDs lazily for decisions that
actually enclose a match. Only branch decisions need to survive while sibling
alternatives settle. The validator does not retain the classification records,
group them by location, or walk the instance a second time.
