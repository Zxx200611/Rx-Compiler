#!/usr/bin/env bash
set -euo pipefail

tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$tools_dir/build/smoke"

cmake -S "$tools_dir/smoke" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel "${ANTLR_BUILD_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
printf '\nParse-tree smoke example:\n'
"$build_dir/rx-parse-smoke" "$tools_dir/smoke/valid.rx"
