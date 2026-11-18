# Unicode data

`tables.inc` holds the Unicode data the IDNA formats need (design doc 5.4). It is
generated, never edited:

```sh
tools/unicode/generate_tables.py            # downloads the pinned UCD version
tools/unicode/generate_tables.py --ucd DIR  # or reads ucd/... and idna/... from DIR
```

The generator pins `UNICODE_VERSION` (now 17.0.0). To move to a new Unicode version,
change that constant, regenerate, and run the tests. The RFC 5892 exception list in the
generator changes only if the IETF updates it.

The tables are derived from the Unicode Character Database and the UTS #46
`IdnaMappingTable.txt`. They are Copyright (c) Unicode, Inc., under the Unicode License v3
in [LICENSE](LICENSE).

What the tables hold:

| Table | Source | Used for |
|---|---|---|
| `idna2008_table` | computed with the RFC 5892 §3 rules | PVALID / CONTEXTJ / CONTEXTO / DISALLOWED / UNASSIGNED |
| `bidi_table` | `DerivedBidiClass.txt` | the RFC 5893 Bidi rule |
| `joining_table` | `DerivedJoiningType.txt` | ZERO WIDTH NON-JOINER (RFC 5892 A.1) |
| `script_table` | `Scripts.txt` (Greek, Hebrew, Hiragana, Katakana, Han) | RFC 5892 A.4–A.7 |
| `mark_table` | `UnicodeData.txt` (Mn, Mc, Me) | no leading combining mark |
| `ccc_table` | `UnicodeData.txt` | NFC; virama (RFC 5892 A.1, A.2) |
| `uts46_*` | `IdnaMappingTable.txt` | UTS #46 mapping, nontransitional |
| `decomposition_table`, `composition_table` | `UnicodeData.txt`, `DerivedNormalizationProps.txt` | NFC |

They were checked against Unicode's own conformance files for 17.0.0:
- `NormalizationTest.txt`: all lines pass.
- `IdnaTestV2.txt`: nothing is accepted that UTS #46 rejects. 160 names UTS #46 accepts are
  rejected, all for a code point UTS #46 marks NV8 or XV8 (not valid under IDNA2008).
- The computed IDNA2008 property agrees with the NV8 and XV8 markings for every code point.
