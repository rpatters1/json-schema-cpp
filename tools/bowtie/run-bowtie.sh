#!/bin/sh
# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Builds the harness image and runs Bowtie's validation and annotation suites against it,
# using the test-suite checkout in third_party/. Run from anywhere in the repository.
#
# Needs: a container engine (Docker, Colima or Podman) and Bowtie
# (`pipx install bowtie-json-schema`). With Colima, Bowtie needs DOCKER_HOST, which is set
# below if it isn't already.
set -eu
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
image=localhost/cpp-json-schema-cpp
suite="$root/third_party/JSON-Schema-Test-Suite"
out="${BOWTIE_OUT:-$root/build/bowtie}"

if [ -z "${DOCKER_HOST:-}" ] && [ -S "$HOME/.colima/default/docker.sock" ]; then
    export DOCKER_HOST="unix://$HOME/.colima/default/docker.sock"
fi
export BOWTIE_ENGINE="${BOWTIE_ENGINE:-docker}"

mkdir -p "$out"
docker build -q -f "$root/tools/bowtie/Dockerfile" -t "$image" "$root" >/dev/null
bowtie suite -i "$image" --dialect 2020-12 "$suite/validation/tests" >"$out/validation.jsonl" 2>"$out/validation.log"
bowtie annotation-suite -i "$image" --dialect 2020-12 "$suite/annotations/tests" >"$out/annotations.jsonl" 2>"$out/annotations.log"

# `bowtie summary` exits non-zero when anything failed or errored; the summaries are the result.
echo "validation suite:"
bowtie summary --show failures <"$out/validation.jsonl" || true
echo "annotation suite:"
bowtie summary --show failures <"$out/annotations.jsonl" || true
echo "reports: $out"
