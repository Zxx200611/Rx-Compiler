#!/usr/bin/env bash
set -euo pipefail

tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
runtime_source="$tools_dir/antlr4-cpp-runtime-4.13.2"
build_dir="$tools_dir/build/antlr4-runtime"
install_dir="$tools_dir/installed/antlr4-4.13.2"

for command_name in cmake ninja g++ sha256sum; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'Missing command: %s. See tools/README.md.\n' "$command_name" >&2
        exit 1
    fi
done

(cd -- "$tools_dir" && sha256sum --check SHA256SUMS)
if [[ "$(tr -d '\r\n' < "$runtime_source/VERSION")" != '4.13.2' ]]; then
    printf 'Expected ANTLR C++ runtime 4.13.2.\n' >&2
    exit 1
fi

# Static-only, no upstream tests/demo: no GoogleTest download or system install.
cmake -S "$runtime_source" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DANTLR4_INSTALL=ON \
    -DANTLR_BUILD_CPP_TESTS=OFF \
    -DANTLR_BUILD_SHARED=OFF \
    -DANTLR_BUILD_STATIC=ON \
    -DWITH_DEMO=OFF
cmake --build "$build_dir" --parallel "${ANTLR_BUILD_JOBS:-4}"
cmake --install "$build_dir"
printf '\nANTLR 4.13.2 runtime installed at: %s\n' "$install_dir"

