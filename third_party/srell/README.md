# SRELL (vendored)

[SRELL](https://www.akenotsuki.com/misc/srell/en/) ("std::regex-like library") by
Nozomu Katoo. It is a header-only ECMAScript regular-expression engine, used here as the
default backend for `pattern`, `patternProperties` and the `regex` format (design doc 5.2).

| | |
|---|---|
| Version | 2026.10 (released 2026-11-11) |
| Source | `https://www.akenotsuki.com/misc/srell/releases/srell2026_10.zip` |
| SHA-256 of the zip | `3bf919a058ed01ca18bc314cdb9e1182971793fc63ddb7361f00e9b560a412cf` |
| Unicode data | 18.0.0 |
| Licence | BSD-2-Clause (`LICENSE`); the generated Unicode data tables also Unicode-3.0 (`LICENSE-Unicode`) |

Only the three headers SRELL needs are kept: `srell.hpp`, `srell_ucfdata2.h` and
`srell_updata3.h`. They are unmodified.

To update, run `tools/update-srell.sh VERSION SHA256`, for example
`tools/update-srell.sh 2026.10 3bf9…`. Then update this table, and run the
`ecmascript-regex` suites.

To use an installed copy instead, such as vcpkg's `srell` port, configure with
`-DJSONSCHEMA_USE_SYSTEM_SRELL=ON`.
