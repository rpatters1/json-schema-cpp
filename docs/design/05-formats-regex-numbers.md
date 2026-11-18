# 5. Formats, regex, numbers

## 5.1 Formats

`format` is an annotation by default (format-annotation vocabulary). It becomes an
assertion when:
- `compile_options::formats = format_mode::assert`, or
- the meta-schema in effect requires the format-assertion vocabulary.

Unknown formats are ignored in annotation mode. In assertion mode they are a compile
error, as the format-assertion vocabulary specifies.

| Format | Standard | Source |
|--------|----------|--------|
| `date-time` `date` `time` | RFC 3339 (incl. leap seconds) | adapt predecessor `string-format-check.cpp` |
| `duration` | RFC 3339 Appendix A | new |
| `email` | RFC 5321 | predecessor `smtp-address-validator.cpp` (Ragel-generated; keep credit) |
| `idn-email` | RFC 6531 | extend the above |
| `hostname` | RFC 1123 | adapt |
| `idn-hostname` | RFC 5890–5893 (IDNA2008) | new; needs Unicode data tables (5.4) |
| `ipv4` `ipv6` | RFC 2673 / 4291 | adapt; tighten against the suite |
| `uri` `uri-reference` | RFC 3986 | reuse the library's own `Uri` parser |
| `iri` `iri-reference` | RFC 3987 | new, on top of `Uri` |
| `uuid` | RFC 4122 | new |
| `uri-template` | RFC 6570 | new |
| `json-pointer` | RFC 6901 | new |
| `relative-json-pointer` | draft-handrews-relative-json-pointer | new |
| `regex` | ECMA-262 | the regex engine's own syntax check |

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
- IDNA2008 for `idn-hostname` needs Unicode derived-property tables (about 50–100 KB
  generated from the UCD). A generator script lives in `tools/` and its output is
  committed, so there's no runtime dependency. ICU is deliberately avoided.
