#!/usr/bin/env bash
set -euo pipefail

tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$tools_dir/.." && pwd)"
jar="$tools_dir/antlr-4.13.2-complete.jar"
output_dir="${1:-$tools_dir/generated}"

if (( $# > 1 )); then
    printf 'Usage: bash tools/generate-antlr4.sh [output-directory]\n' >&2
    exit 1
fi
if ! command -v java >/dev/null 2>&1; then
    printf 'Java 11+ is required. See tools/README.md.\n' >&2
    exit 1
fi
mkdir -p -- "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"
mkdir -p -- "$output_dir/grammar"
# Distinct build-copy names avoid the C++ runtime's Lexer/Parser class names.
sed 's/^lexer grammar Lexer;/lexer grammar RxLexer;/' \
    "$repo_dir/template/grammar/Lexer.g4" > "$output_dir/grammar/RxLexer.g4"
sed -e 's/^parser grammar Parser;/parser grammar RxParser;/' \
    -e 's/tokenVocab=Lexer/tokenVocab=RxLexer/' \
    "$repo_dir/template/grammar/Parser.g4" > "$output_dir/grammar/RxParser.g4"

java -jar "$jar" -Dlanguage=Cpp -package rxantlr -visitor -no-listener \
    -Xexact-output-dir -o "$output_dir" "$output_dir/grammar/RxLexer.g4"
java -jar "$jar" -Dlanguage=Cpp -package rxantlr -visitor -no-listener \
    -Xexact-output-dir -lib "$output_dir" -o "$output_dir" \
    "$output_dir/grammar/RxParser.g4"
printf 'Generated C++ lexer, parser and visitor at: %s\n' "$output_dir"
