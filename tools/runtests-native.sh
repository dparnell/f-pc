#!/usr/bin/env bash
# Run tests/*.seq on the native VM (vm/fpc --batch) and compare with
# tests/expected/<name>.out. With --record, write the expected files instead
# (review the diff against tests/golden/<name>.KERNEL.out before committing;
# every difference must be explained in tests/expected/DIVERGENCES.md).
#
# usage: tools/runtests-native.sh [--record] [test.seq ...]
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FPC=${FPC:-$ROOT/vm/fpc}
record=0
[ "${1:-}" = --record ] && { record=1; shift; }
tests=("$@"); [ ${#tests[@]} -eq 0 ] && tests=("$ROOT"/tests/*.seq)
mkdir -p "$ROOT/tests/expected"
out=$(mktemp -d)
pass=0 fail=0
for t in "${tests[@]}"; do
    base=$(basename "$t" .seq)
    exp=$ROOT/tests/expected/$base.out
    (cd "$ROOT/tests" && timeout 20 "$FPC" --batch "$t" < /dev/null) > "$out/$base.out" 2>&1
    if [ $record = 1 ]; then
        cp "$out/$base.out" "$exp"; echo "$base: recorded"
    elif [ ! -f "$exp" ]; then
        fail=$((fail+1)); echo "$base: no expected output"
    elif diff -u "$exp" "$out/$base.out" > "$out/$base.diff"; then
        pass=$((pass+1)); echo "$base: ok"
    else
        fail=$((fail+1)); echo "$base: FAIL"; head -30 "$out/$base.diff"
    fi
done
[ $record = 1 ] && exit 0
echo "$pass passed, $fail failed"
[ $fail = 0 ]
