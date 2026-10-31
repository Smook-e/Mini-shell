#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly root_dir="$(cd -- "$script_dir/.." && pwd)"
readonly binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

g++ -std=c++17 -Wall -Wextra \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DCOMMAND_TEST_BUILD \
    "$root_dir/command.cc" "$root_dir/tokenizer.cc" \
    "$root_dir/command_tests.cc" -lreadline -o "$binary"

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
"$binary"
