#!/usr/bin/env bash
# Run tests/*.seq on the native VM and compare with tests/expected/<name>.out.
# Each test is run by F-PC itself: "fpc --batch - FLOAD <test> BYE".
# With --record, write the expected files instead (review the diff against
# tests/golden/<name>.KERNEL.out before committing; every difference must be
# explained in tests/expected/DIVERGENCES.md).
#
# usage: tools/runtests-native.sh [--record] [test.seq ...]
# Tests listed in tests/expected/SKIP are not run natively.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FPC=${FPC:-$ROOT/vm/fpc}
record=0
[ "${1:-}" = --record ] && { record=1; shift; }
tests=("$@"); [ ${#tests[@]} -eq 0 ] && tests=("$ROOT"/tests/*.seq "$ROOT"/tests/full/*.seq)
IMG=${FULL_IMAGE:-$ROOT/F-PC.IMG}
mkdir -p "$ROOT/tests/expected"
out=$(mktemp -d)
pass=0 fail=0 skip=0
for t in "${tests[@]}"; do
    base=$(basename "$t" .seq)
    if grep -qx "$base" "$ROOT/tests/expected/SKIP" 2>/dev/null; then skip=$((skip+1)); continue; fi
    exp=$ROOT/tests/expected/$base.out
    args=()
    if [ "$(basename "$(dirname "$t")")" = full ]; then  # needs the full system
        if [ ! -f "$IMG" ]; then skip=$((skip+1)); echo "$base: skipped (no $IMG)"; continue; fi
        args=(-i "$IMG")
    fi
    # the full system's status line goes to the batch stream: drop it
    (cd "$(dirname "$t")" && timeout 20 "$FPC" --batch "${args[@]}" - FLOAD "$base.seq" BYE < /dev/null) \
        | sed -e 's/ C - [0-9]*k : - [0-9]*k .*[0-9][0-9]:[0-9][0-9] //g' > "$out/$base.out" 2>&1
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
echo "$pass passed, $fail failed, $skip skipped"
[ $fail = 0 ]
