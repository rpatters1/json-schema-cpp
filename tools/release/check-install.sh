#!/bin/sh
# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Checks an installed json-schema-cpp the way a consumer would use it:
#   tools/release/check-install.sh PREFIX WORKDIR
#
# Builds tests/package against PREFIX with find_package() and runs it, then compiles the
# same program with the flags from `pkg-config jsonschema` (skipped without pkg-config).
set -eu
if [ $# -ne 2 ]; then
    echo "usage: $0 PREFIX WORKDIR" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$1" "$2"
prefix=$(cd "$1" && pwd)
work=$(cd "$2" && pwd)

echo "== find_package(jsonschema)"
cmake -S "$root/tests/package" -B "$work/cmake" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/cmake" --config Release
ctest --test-dir "$work/cmake" -C Release --output-on-failure

if command -v pkg-config >/dev/null 2>&1; then
    echo "== pkg-config jsonschema"
    # lib, lib64 or a multiarch directory such as lib/x86_64-linux-gnu.
    PKG_CONFIG_PATH=$(find "$prefix" -type d -name pkgconfig | tr '\n' ':')${PKG_CONFIG_PATH:-}
    export PKG_CONFIG_PATH
    echo "version: $(pkg-config --modversion jsonschema)"
    # shellcheck disable=SC2046
    ${CXX:-c++} -std=c++20 "$root/tests/package/main.cpp" -o "$work/pkg-config-test" \
        $(pkg-config --cflags --libs jsonschema)
    libdirs=$(dirname "$(find "$prefix" -name 'jsonschema.pc')")/..:
    LD_LIBRARY_PATH="$libdirs${LD_LIBRARY_PATH:-}" DYLD_LIBRARY_PATH="$libdirs${DYLD_LIBRARY_PATH:-}" \
        "$work/pkg-config-test"
fi
