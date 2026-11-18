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

The design uses an internal interface:

```cpp
class regex_engine {
public:
    virtual ~regex_engine() = default;
    virtual std::unique_ptr<compiled_regex> compile(std::string_view pattern) const = 0;   // throws schema_error
};
class compiled_regex {
public:
    virtual ~compiled_regex() = default;
    virtual bool search(std::string_view utf8) const = 0;
};
```

Backends:
- **PCRE2 (default, recommended).** Use UTF and UCP mode, plus a small translation pass
  for ECMA-262 constructs PCRE2 reads differently (`\d`/`\w` must stay ASCII, `$` has no
  trailing-newline special case, no possessive or atomic syntax, and named groups are
  `(?<n>)` only). PCRE2's JIT keeps matching fast.
- **`std::regex`** as a fallback when built without PCRE2, with optional
  `ecmascript-regex` tests documented as expected failures.
- Callers may supply their own engine through `compile_options::regex`.

Whether to translate ECMA-262 to PCRE2, or write a small dedicated ECMA-262 regex
engine, is open (doc 7). Translation is far less work. A dedicated engine removes a
dependency and makes `ecmascript-regex` compliance exact.

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
