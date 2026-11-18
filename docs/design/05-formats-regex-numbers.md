# 5. Formats, regex, numbers

## 5.1 Formats

`format` is an annotation by default (format-annotation vocabulary). It also asserts
when either of these holds:
- `compile_options::formats = format_mode::assert`;
- the schema's meta-schema lists the format-assertion vocabulary, as `true` or `false`.
  The library supports that vocabulary, so it applies either way.

Either way, `format` is still reported as an annotation. In assertion mode:
- An unknown format is a compile error, as the format-assertion vocabulary specifies.
  `compile_options::extra_formats` registers more format names, or replaces built-in
  ones.
- Built-in checkers accept non-string instances. Custom checkers see every type.

The checkers are in `src/formats.cpp` and `src/unicode/`, written for this library
(milestones 4b and 4c):

| Format | Standard | How |
|--------|----------|-----|
| `date` `time` `date-time` | RFC 3339 | The offset is required. A leap second (`:60`) is valid only at 23:59 UTC after applying the offset. Lowercase `t`/`z` are allowed. |
| `duration` | RFC 3339 Appendix A | Nested components (`P1Y2D` is invalid), whole numbers, and weeks stand alone. |
| `email` | RFC 5321 Mailbox | Dot-string or quoted local part (at most 64 octets); a domain, or an address literal (IPv4, `IPv6:`, general). |
| `hostname` | RFC 1123, plus RFC 5890 A-labels | LDH labels of 1–63 characters, 253 in all. A label starting `xn--` (any case) must be a valid A-label: canonical Punycode that decodes to a valid U-label, as for `idn-hostname`, with the Bidi rule across the name. Other labels with "--" in positions 3–4 are allowed. |
| `ipv4` `ipv6` | RFC 3986 §3.2.2 | The `uri` parser's own address grammar. |
| `uri` `uri-reference` | RFC 3986 | The `uri` parser: absolute, or any reference. |
| `iri` `iri-reference` | RFC 3987 | Each non-ASCII character must be `ucschar` (or `iprivate` in the query). It is percent-encoded, and the result is parsed as a URI. |
| `uuid` | RFC 9562 | 8-4-4-4-12 hexadecimal digits, any case, any version. |
| `uri-template` | RFC 6570 | Expressions with operators, variable lists, prefix and explode modifiers. Apostrophes are allowed in literals, as the suite expects. |
| `json-pointer` `relative-json-pointer` | RFC 6901, draft-handrews-relative-json-pointer-01 | |
| `regex` | ECMA-262 | Compiles with the regex backend (5.2). Global inline flag groups such as `(?i)` are rejected, as ECMA-262 does not have them. |
| `idn-hostname` | UTS #46, RFC 5890–5893 | UTS #46 processing (nontransitional, with STD3 rules, hyphen checks, CheckJoiners, CheckBidi and DNS lengths), then every label must also be a valid IDNA2008 label: each code point PVALID, or CONTEXTJ/CONTEXTO with its rule satisfied. So full-width characters and the ideographic full stop are mapped, and symbols UTS #46 allows only for compatibility (such as ≠) are rejected. A trailing root dot is rejected. See 5.4. |
| `idn-email` | RFC 6531 | As `email`, but atoms and quoted strings may contain any non-ASCII character, and the domain is an `idn-hostname`. |

## 5.2 Regex

`pattern` and `patternProperties` must follow ECMA-262 with Unicode semantics:
`\p{...}` classes, `\d` meaning ASCII only, matching by code point, and no implicit
anchoring. `std::regex` can't meet this, because it doesn't support Unicode and differs
between standard-library implementations.

The library uses a small internal interface, `compiled_regex` with `search(utf8)`. The
backend is chosen at build time with `JSONSCHEMA_REGEX` (decided in milestone 4a):

- **SRELL (default).** SRELL is a header-only ECMAScript engine by Nozomu Katoo, vendored
  in `third_party/srell` (BSD-2-Clause, with Unicode-3.0 data tables).
  - It implements ECMA-262 itself, so there is no translation layer: the `u` flag is
    always on, with `\p{...}`, lookbehind and named groups, and matching by code point
    on UTF-8 input.
  - Searches are `const`, with per-call state, so a compiled schema can be shared
    between threads.
  - SRELL's backtracking limit turns a pathological pattern-and-input pair into an
    `evaluation_error`, so it never hangs or silently fails to match.
  - With it, every regex test in the suite passes: `pattern`, `patternProperties`,
    `ecmascript-regex` and `non-bmp-regex`.
  - `JSONSCHEMA_USE_SYSTEM_SRELL` uses an installed copy, such as vcpkg's `srell` port.
- **`std::regex`** (`JSONSCHEMA_REGEX=std`), for builds that want no third-party code.
  It works on bytes, has no `\p{...}`, and libstdc++ and libc++ differ on details such
  as `\cX`. Its known failures are listed with `"regex": "std"` in
  `expected-failures.json`, and a CI job builds this configuration.

Why SRELL over the earlier PCRE2 proposal:
- It is header-only, so there is nothing to link.
- It gives exact ECMA-262 semantics, where PCRE2 would need a translation pass that has
  to refuse what it can't translate.
- SRELL has no official Git repository; releases are zip files. It is vendored at a
  pinned version (`tools/update-srell.sh` checks the SHA-256) rather than fetched at
  build time, so builds need no network.

A caller-supplied engine (`compile_options::regex` in doc 3.1) is not implemented. There
has been no need for one.

## 5.3 Numbers

- `type: integer` accepts any number with no fractional part, including `1.0` and `1e2`.
- Comparisons across int64, uint64 and double are exact. Integers aren't converted to
  double when that would lose precision.
- `multipleOf`:
  - integer instance and integer divisor: `%`.
  - otherwise: compute the quotient as a double, and if it's finite, check whether it's
    an integer. If overflow or precision rules that out, use a decimal-string path. The
    rules here are tuned against the suite's `float-overflow` and `multipleOf` cases.
- **Big numbers.** nlohmann stores numbers as int64, uint64 or double, so integers
  beyond 64 bits arrive as rounded doubles before the validator sees them. The optional
  `bignum` tests can't fully pass on stock `nlohmann::json`. This is the one exception
  expected to be permanent, unless a later backend keeps the original number text.

## 5.4 Unicode

- `minLength`/`maxLength` count code points, not bytes. This needs a fast UTF-8
  decoder, about 30 lines.
- **IDNA** (milestone 4c). `idn-hostname`, `idn-email` and A-labels in `hostname` need
  Unicode data. `tools/unicode/generate_tables.py` generates `src/unicode/tables.inc`
  from a pinned UCD version (now 17.0.0), and the output is committed, so there's no
  runtime dependency. ICU is deliberately avoided. The tables come to about 140 KB:
  - the UTS #46 mapping, about 77 KB;
  - NFC decompositions and compositions, about 37 KB;
  - the IDNA2008 derived property, computed with the RFC 5892 rules rather than copied
    from IANA, plus Bidi_Class, Joining_Type, the five scripts the contextual rules name,
    marks and combining classes: about 26 KB together.
- `src/unicode/` also holds UTF-8 decoding, NFC and Punycode (RFC 3492).
- The code was checked against Unicode's own 17.0.0 conformance files
  (`src/unicode/README.md`). Every `NormalizationTest.txt` line passes. Against
  `IdnaTestV2.txt`, nothing UTS #46 rejects is accepted, and the only names UTS #46
  accepts but we reject contain code points it marks as not valid under IDNA2008.
