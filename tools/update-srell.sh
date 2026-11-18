#!/bin/sh
# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Replaces third_party/srell with a SRELL release.
#   tools/update-srell.sh VERSION SHA256     (e.g. 2026.10 3bf919a0...)
# The zip is downloaded into a temporary directory and checked against SHA256 before
# anything is copied.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 VERSION SHA256" >&2; exit 2; }
version=$1
expected=$2
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

url="https://www.akenotsuki.com/misc/srell/releases/srell$(echo "$version" | tr . _).zip"
curl -fsSL -o "$work/srell.zip" "$url"
actual=$(shasum -a 256 "$work/srell.zip" | cut -d' ' -f1)
if [ "$actual" != "$expected" ]; then
    echo "SHA-256 mismatch for $url: got $actual" >&2
    exit 1
fi
unzip -q "$work/srell.zip" -d "$work/x"
for f in srell.hpp srell_ucfdata2.h srell_updata3.h; do
    cp "$work/x/$f" "$root/third_party/srell/$f"
done
cp "$work/x/license.txt" "$root/third_party/srell/LICENSE"
echo "updated third_party/srell to SRELL $version; now update third_party/srell/README.md"
