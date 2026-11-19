#!/bin/sh
# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Builds the release source tarball, json-schema-cpp-VERSION.tar.gz, and SHA256SUMS:
#   tools/release/make-dist.sh VERSION OUTDIR [REF]
#
# REF (default HEAD) must be committed. Unlike GitHub's automatic archives, the tarball
# includes the JSON-Schema-Test-Suite submodule, so the whole test suite runs from it
# offline. The output is reproducible: the same REF always gives the same bytes.
#
# Needs: git, python3, and sha256sum or shasum.
set -eu
if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "usage: $0 VERSION OUTDIR [REF]" >&2
    exit 2
fi
version=$1
outdir=$2
ref=${3:-HEAD}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
name=json-schema-cpp-$version
mkdir -p "$outdir"
outdir=$(cd "$outdir" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

commit=$(git -C "$root" rev-parse --verify "$ref^{commit}")
git -C "$root" archive --format=tar --prefix="$name/" "$commit" >"$work/main.tar"
# Each submodule at the commit the superproject records, not whatever is checked out.
git -C "$root" ls-tree -r "$commit" | awk '$2 == "commit" { print $3, $4 }' >"$work/submodules"
i=0
while read -r sha path; do
    i=$((i + 1))
    if ! git -C "$root/$path" cat-file -e "$sha^{commit}" 2>/dev/null; then
        echo "$0: submodule $path is missing commit $sha (git submodule update --init)" >&2
        exit 1
    fi
    git -C "$root/$path" archive --format=tar --prefix="$name/$path/" "$sha" >"$work/sub$i.tar"
done <"$work/submodules"

# Merge the archives in name order, with the superproject's commit time on every entry
# and no owner names, then gzip without a timestamp.
mtime=$(git -C "$root" show -s --format=%ct "$commit")
python3 -I - "$work" "$outdir/$name.tar.gz" "$mtime" <<'PY'
import glob, gzip, io, os, sys, tarfile
work, out, mtime = sys.argv[1], sys.argv[2], int(sys.argv[3])
members = {}
for path in [os.path.join(work, "main.tar")] + sorted(glob.glob(os.path.join(work, "sub*.tar"))):
    with tarfile.open(path) as tar:
        for m in tar.getmembers():
            if m.type in (tarfile.XGLTYPE, tarfile.XHDTYPE):
                continue
            data = tar.extractfile(m).read() if m.isfile() else None
            members[m.name.rstrip("/")] = (m, data)   # submodule contents replace the gitlink
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as tar:
    for name in sorted(members):
        m, data = members[name]
        m.name, m.mtime = name, mtime
        m.uid = m.gid = 0
        m.uname = m.gname = ""
        m.pax_headers = {}
        tar.addfile(m, io.BytesIO(data) if data is not None else None)
with open(out, "wb") as f, gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0, compresslevel=9) as gz:
    gz.write(buf.getvalue())
PY

cd "$outdir"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$name.tar.gz" >SHA256SUMS
else
    shasum -a 256 "$name.tar.gz" >SHA256SUMS
fi
cat SHA256SUMS
