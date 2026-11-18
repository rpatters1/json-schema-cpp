#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
"""Generates src/unicode/tables.inc from the Unicode Character Database.

    tools/unicode/generate_tables.py [--ucd DIR]

Without --ucd, the files for UNICODE_VERSION are downloaded from unicode.org into a
temporary directory. With --ucd, DIR must hold them under the same relative paths as
FILES below (ucd/..., idna/...). The output is committed, so building the library never
needs the UCD (design doc 5.4).

The tables serve `idn-hostname`, `idn-email` and A-labels in `hostname`:
- UTS #46 mapping (IdnaMappingTable.txt), nontransitional;
- the IDNA2008 derived property of every code point, computed with the RFC 5892 rules;
- the properties the RFC 5892 contextual rules and the RFC 5893 Bidi rule need: Bidi_Class,
  Joining_Type, Script (the five scripts the rules name), General_Category=Mark;
- Canonical_Combining_Class and the canonical decompositions and compositions, for NFC.
"""

import argparse
import os
import tempfile
import urllib.request

UNICODE_VERSION = "17.0.0"
FILES = [
    "ucd/UnicodeData.txt",
    "ucd/PropList.txt",
    "ucd/DerivedCoreProperties.txt",
    "ucd/DerivedNormalizationProps.txt",
    "ucd/HangulSyllableType.txt",
    "ucd/Scripts.txt",
    "ucd/extracted/DerivedBidiClass.txt",
    "ucd/extracted/DerivedJoiningType.txt",
    "idna/IdnaMappingTable.txt",
]
MAX_CP = 0x10FFFF
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUTPUT = os.path.join(ROOT, "src", "unicode", "tables.inc")

# Value orders are shared with src/unicode/unicode.hpp.
IDNA2008 = ["PVALID", "CONTEXTJ", "CONTEXTO", "DISALLOWED", "UNASSIGNED"]
BIDI = ["L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON",
        "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"]
JOINING = ["U", "C", "D", "L", "R", "T"]
SCRIPTS = ["Other", "Greek", "Hebrew", "Hiragana", "Katakana", "Han"]
UTS46 = ["valid", "mapped", "ignored", "disallowed"]

# RFC 5892 §2.6 Exceptions (F).
EXCEPTIONS = {
    0x00DF: "PVALID", 0x03C2: "PVALID", 0x06FD: "PVALID", 0x06FE: "PVALID",
    0x0F0B: "PVALID", 0x3007: "PVALID",
    0x00B7: "CONTEXTO", 0x0375: "CONTEXTO", 0x05F3: "CONTEXTO", 0x05F4: "CONTEXTO",
    0x30FB: "CONTEXTO",
    0x0640: "DISALLOWED", 0x07FA: "DISALLOWED", 0x302E: "DISALLOWED", 0x302F: "DISALLOWED",
    0x3031: "DISALLOWED", 0x3032: "DISALLOWED", 0x3033: "DISALLOWED", 0x3034: "DISALLOWED",
    0x3035: "DISALLOWED", 0x303B: "DISALLOWED",
}
for cp in range(0x0660, 0x066A):
    EXCEPTIONS[cp] = "CONTEXTO"
for cp in range(0x06F0, 0x06FA):
    EXCEPTIONS[cp] = "CONTEXTO"
# RFC 5892 §2.7 BackwardCompatible (G) is empty.
IGNORABLE_BLOCKS = [(0x20D0, 0x20FF), (0x1D100, 0x1D1FF), (0x1D200, 0x1D24F)]   # §2.5 (E)
LETTER_DIGITS = {"Ll", "Lu", "Lo", "Nd", "Lm", "Mn", "Mc"}                      # §2.1 (A)


def fetch(directory):
    for path in FILES:
        target = os.path.join(directory, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        url = f"https://www.unicode.org/Public/{UNICODE_VERSION}/{path}"
        print("downloading", url)
        with urllib.request.urlopen(url) as response, open(target, "wb") as out:
            out.write(response.read())


def records(directory, path):
    """Yields (first, last, fields) for each data line; comments and blanks are skipped."""
    with open(os.path.join(directory, path), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [x.strip() for x in line.split(";")]
            span = fields[0].split("..")
            first = int(span[0], 16)
            last = int(span[-1], 16)
            yield first, last, fields[1:]


def missing_defaults(directory, path):
    """Yields (first, last, value) for the file's @missing lines, in order."""
    with open(os.path.join(directory, path), encoding="utf-8") as f:
        for line in f:
            if line.startswith("# @missing:"):
                span, value = [x.strip() for x in line[len("# @missing:"):].split(";")[:2]]
                first, last = (int(x, 16) for x in span.split(".."))
                yield first, last, value


def check_version(directory, path):
    with open(os.path.join(directory, path), encoding="utf-8") as f:
        head = f.read(2000)
    if UNICODE_VERSION.rsplit(".", 1)[0] not in head and UNICODE_VERSION not in head:
        raise SystemExit(f"{path} does not look like Unicode {UNICODE_VERSION}")


def load(directory):
    for path in FILES:
        if path != "ucd/UnicodeData.txt":
            check_version(directory, path)

    gc = ["Cn"] * (MAX_CP + 1)
    ccc = [0] * (MAX_CP + 1)
    decomposition = {}
    range_start = None
    for first, _, fields in records(directory, "ucd/UnicodeData.txt"):
        name, category, combining, decomp = fields[0], fields[1], int(fields[2]), fields[4]
        if name.endswith(", First>"):
            range_start = first
            continue
        start = range_start if name.endswith(", Last>") else first
        range_start = None
        for cp in range(start, first + 1):
            gc[cp] = category
            ccc[cp] = combining
        if decomp and not decomp.startswith("<"):
            decomposition[first] = [int(x, 16) for x in decomp.split()]

    def binary_property(path, name):
        result = set()
        for first, last, fields in records(directory, path):
            if fields[0] == name:
                result.update(range(first, last + 1))
        return result

    props = {
        "noncharacter": binary_property("ucd/PropList.txt", "Noncharacter_Code_Point"),
        "white_space": binary_property("ucd/PropList.txt", "White_Space"),
        "join_control": binary_property("ucd/PropList.txt", "Join_Control"),
        "default_ignorable": binary_property("ucd/DerivedCoreProperties.txt", "Default_Ignorable_Code_Point"),
        "cwkcf": binary_property("ucd/DerivedNormalizationProps.txt", "Changes_When_NFKC_Casefolded"),
        "exclusion": binary_property("ucd/DerivedNormalizationProps.txt", "Full_Composition_Exclusion"),
    }
    old_jamo = set()
    for first, last, fields in records(directory, "ucd/HangulSyllableType.txt"):
        if fields[0] in ("L", "V", "T"):
            old_jamo.update(range(first, last + 1))

    def enumerated(path, values, default):
        table = [values.index(default)] * (MAX_CP + 1)
        for first, last, value in missing_defaults(directory, path):
            for cp in range(first, last + 1):
                table[cp] = values.index(value) if value in values else values.index(default)
        for first, last, fields in records(directory, path):
            for cp in range(first, last + 1):
                table[cp] = values.index(fields[0])
        return table

    bidi = enumerated("ucd/extracted/DerivedBidiClass.txt", BIDI, "L")
    joining = enumerated("ucd/extracted/DerivedJoiningType.txt", JOINING, "U")

    script = [0] * (MAX_CP + 1)
    for first, last, fields in records(directory, "ucd/Scripts.txt"):
        if fields[0] in SCRIPTS:
            for cp in range(first, last + 1):
                script[cp] = SCRIPTS.index(fields[0])

    # RFC 5892 §3: the derived property value.
    def idna2008(cp):
        if cp in EXCEPTIONS:
            return EXCEPTIONS[cp]
        if gc[cp] == "Cn" and cp not in props["noncharacter"]:
            return "UNASSIGNED"
        if cp == 0x2D or 0x30 <= cp <= 0x39 or 0x61 <= cp <= 0x7A:
            return "PVALID"
        if cp in props["join_control"]:
            return "CONTEXTJ"
        if cp in props["cwkcf"]:      # Unstable (B): NFKC_Casefold(cp) != cp
            return "DISALLOWED"
        if cp in props["default_ignorable"] or cp in props["white_space"] or cp in props["noncharacter"]:
            return "DISALLOWED"
        if any(lo <= cp <= hi for lo, hi in IGNORABLE_BLOCKS):
            return "DISALLOWED"
        if cp in old_jamo:
            return "DISALLOWED"
        if gc[cp] in LETTER_DIGITS:
            return "PVALID"
        return "DISALLOWED"

    idna = [IDNA2008.index(idna2008(cp)) for cp in range(MAX_CP + 1)]
    mark = [1 if gc[cp] in ("Mn", "Mc", "Me") else 0 for cp in range(MAX_CP + 1)]

    uts46 = []          # (first, status, mapping or None)
    for first, last, fields in records(directory, "idna/IdnaMappingTable.txt"):
        status = fields[0]
        mapping = None
        if status == "deviation":       # nontransitional processing: deviations are valid
            status = "valid"
        elif status == "mapped":
            mapping = tuple(int(x, 16) for x in fields[1].split())
        elif status not in UTS46:
            raise SystemExit(f"unexpected UTS #46 status {status!r}")
        uts46.append((first, last, status, mapping))

    compositions = sorted(
        (d[0], d[1], cp) for cp, d in decomposition.items()
        if len(d) == 2 and cp not in props["exclusion"])

    return {
        "idna": idna, "bidi": bidi, "joining": joining, "script": script, "mark": mark,
        "ccc": ccc, "decomposition": decomposition, "compositions": compositions, "uts46": uts46,
    }


def ranges(values):
    """Run-length encodes a per-code-point table as [(first, value)], starting at 0."""
    out = []
    for cp, value in enumerate(values):
        if not out or out[-1][1] != value:
            out.append((cp, value))
    return out


def emit_array(lines, ctype, name, items, per_line=8):
    lines.append(f"constexpr {ctype} {name}[] = {{")
    for i in range(0, len(items), per_line):
        lines.append("    " + " ".join(f"{x}," for x in items[i:i + per_line]))
    lines.append("};")
    lines.append("")


def emit_ranges(lines, name, comment, values):
    packed = [f"0x{first << 8 | value:08X}" for first, value in ranges(values)]
    lines.append(f"// {comment}")
    lines.append("// Each entry is (first code point << 8) | value; a run lasts until the next entry.")
    emit_array(lines, "std::uint32_t", name, packed)
    return len(packed) * 4


def utf8(cps):
    return "".join(chr(cp) for cp in cps).encode("utf-8")


def generate(data):
    lines = [
        "// SPDX-FileCopyrightText: 2026 Robert Patterson",
        "// SPDX-FileCopyrightText: 1991-2025 Unicode, Inc.",
        "// SPDX-License-Identifier: MIT AND Unicode-3.0",
        "//",
        f"// Generated by tools/unicode/generate_tables.py from Unicode {UNICODE_VERSION}. Do not edit.",
        "// Derived from the Unicode Character Database and IdnaMappingTable.txt, used under the",
        "// Unicode License v3 (src/unicode/LICENSE). Included only by src/unicode/unicode.cpp.",
        "",
        f'constexpr const char* unicode_version = "{UNICODE_VERSION}";',
        "",
    ]
    size = 0
    size += emit_ranges(lines, "idna2008_table", "RFC 5892 derived property: " + ", ".join(IDNA2008), data["idna"])
    size += emit_ranges(lines, "bidi_table", "Bidi_Class: " + ", ".join(BIDI), data["bidi"])
    size += emit_ranges(lines, "joining_table", "Joining_Type: " + ", ".join(JOINING), data["joining"])
    size += emit_ranges(lines, "script_table", "Script: " + ", ".join(SCRIPTS), data["script"])
    size += emit_ranges(lines, "mark_table", "General_Category=Mark (Mn, Mc, Me): 0 or 1", data["mark"])
    size += emit_ranges(lines, "ccc_table", "Canonical_Combining_Class", data["ccc"])

    # UTS #46: one entry per run of equal status and mapping. Mappings are UTF-8 in a pool.
    pool = bytearray()
    offsets = {}
    entries = []
    for first, last, status, mapping in data["uts46"]:
        value = 0
        if mapping is not None:
            text = utf8(mapping)
            if text not in offsets:
                offsets[text] = len(pool)
                pool += text
            if len(text) > 255 or offsets[text] >= 1 << 24:
                raise SystemExit("UTS #46 mapping does not fit the packing")
            value = offsets[text] << 8 | len(text)
        key = (UTS46.index(status), value)
        if entries and entries[-1][1] == key and mapping is None:
            continue
        entries.append((first, key))
    lines.append("// UTS #46 mapping (nontransitional): " + ", ".join(UTS46) + ".")
    lines.append("// uts46_keys: (first code point << 2) | status; a run lasts until the next entry.")
    lines.append("// uts46_mappings: (offset into uts46_pool << 8) | length in bytes, for mapped runs.")
    emit_array(lines, "std::uint32_t", "uts46_keys", [f"0x{first << 2 | s:08X}" for first, (s, _) in entries])
    emit_array(lines, "std::uint32_t", "uts46_mappings", [f"0x{v:08X}" for _, (_, v) in entries])
    emit_array(lines, "unsigned char", "uts46_pool", [f"0x{b:02X}" for b in pool], per_line=16)
    size += len(entries) * 8 + len(pool)

    decompositions = sorted(data["decomposition"].items())
    lines.append("// Canonical decompositions: {code point, first, second (0 if a singleton)}.")
    lines.append("struct decomposition_entry { std::uint32_t cp, first, second; };")
    lines.append("constexpr decomposition_entry decomposition_table[] = {")
    for cp, d in decompositions:
        lines.append(f"    {{0x{cp:05X}, 0x{d[0]:05X}, 0x{d[1] if len(d) > 1 else 0:05X}}},")
    lines.append("};")
    lines.append("")
    lines.append("// Canonical compositions (excluding Full_Composition_Exclusion), sorted by {first, second}.")
    lines.append("struct composition_entry { std::uint32_t first, second, composite; };")
    lines.append("constexpr composition_entry composition_table[] = {")
    for a, b, cp in data["compositions"]:
        lines.append(f"    {{0x{a:05X}, 0x{b:05X}, 0x{cp:05X}}},")
    lines.append("};")
    size += 12 * (len(decompositions) + len(data["compositions"]))

    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {os.path.relpath(OUTPUT, ROOT)}: about {size // 1024} KB of tables")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ucd", help="directory holding the UCD files (default: download)")
    args = parser.parse_args()
    if args.ucd:
        generate(load(args.ucd))
    else:
        with tempfile.TemporaryDirectory() as directory:
            fetch(directory)
            generate(load(directory))


if __name__ == "__main__":
    main()
