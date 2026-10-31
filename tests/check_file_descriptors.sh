#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly root_dir="$(cd -- "$script_dir/.." && pwd)"
readonly shell_binary="$root_dir/myshell"
readonly work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

descriptor_set() {
    lsof -n -P -a -p "$1" |
        awk 'NR > 1 && $4 ~ /^[0-9]+[rwu]/ {print $4, $5, $9}' |
        sort
}

(
    printf 'true\n'
    sleep 1
    for i in $(seq 1 50); do
        printf 'printf value%d > out%d\n' "$i" "$i"
        printf 'cat out%d | wc -c > count%d\n' "$i" "$i"
    done
    sleep 5
    printf 'exit\n'
) | (
    cd "$work_dir"
    "$shell_binary" >output 2>error
) &
shell_pid=$!

sleep 1
before="$(descriptor_set "$shell_pid")"
sleep 2.5
during="$(descriptor_set "$shell_pid")"

wait "$shell_pid"

if [[ "$before" != "$during" ]]; then
    echo "file-descriptor set changed during repeated pipelines" >&2
    diff -u <(printf '%s\n' "$before") <(printf '%s\n' "$during") || true
    exit 1
fi

[[ "$(find "$work_dir" -maxdepth 1 -name 'out[0-9]*' | wc -l)" -eq 50 ]]
[[ "$(find "$work_dir" -maxdepth 1 -name 'count[0-9]*' | wc -l)" -eq 50 ]]

echo "File-descriptor check passed"
