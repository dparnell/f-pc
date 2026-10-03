#!/usr/bin/env bash
# Run the interactive UI scenarios in tests/ui/*.ui against the full system
# image, in a pseudo-terminal emulated with pyte (pip install pyte).
#
# A .ui file has lines:   size COLSxROWS / setup SHELL / script STEPS /
#                         check SHELL   (see tools/ptytest.py for STEPS)
# usage: tools/runtests-ui.sh [test.ui ...]     env: PYTHON, FPC_IMAGE
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PY=${PYTHON:-python3}
if ! "$PY" -c 'import pyte' 2>/dev/null; then
    echo "UI tests skipped: the pyte module is not installed (pip install pyte)"; exit 0
fi
IMG=${FPC_IMAGE:-$ROOT/F-PC.IMG}
[ -f "$IMG" ] || { echo "no $IMG: run make -C vm image"; exit 1; }
tests=("$@"); [ ${#tests[@]} -eq 0 ] && tests=("$ROOT"/tests/ui/*.ui)
pass=0 fail=0
for t in "${tests[@]}"; do
    name=$(basename "$t" .ui)
    dir=$(mktemp -d)
    size=$(sed -n 's/^size //p' "$t"); script=$(sed -n 's/^script //p' "$t")
    (cd "$dir" && sed -n 's/^setup //p' "$t" | bash)
    if timeout 120 "$PY" "$ROOT/tools/ptytest.py" --size "${size:-80x25}" --cwd "$dir" "$script" \
            -- "$ROOT/vm/fpc" -i "$IMG" > "$dir/out.txt" 2>&1 \
       && (cd "$dir" && sed -n 's/^check //p' "$t" | bash); then
        pass=$((pass+1)); echo "$name: ok"
    else
        fail=$((fail+1)); echo "$name: FAIL"; head -40 "$dir/out.txt"
    fi
    rm -rf "$dir"
done
echo "$pass passed, $fail failed"
[ $fail = 0 ]
