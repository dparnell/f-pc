#!/usr/bin/env bash
# Load each TOOLS/*.SEQ add-on on its own, on top of the full system image,
# and check that it loads without error (fpc's exit status in batch mode).
# usage: tools/loadtools.sh [NAME ...]      env: FPC_IMAGE
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FPC=$ROOT/vm/fpc
IMG=${FPC_IMAGE:-$ROOT/F-PC.IMG}
[ -f "$IMG" ] || { echo "no $IMG: run make -C vm image"; exit 1; }

# files that need another loaded first
declare -A NEEDS=(
    [FPMATH]=FFLOAT  [COMPLEX]=FFLOAT
    [SFLOAT2]=SFLOAT1  [SFLOAT3]="SFLOAT1 SFLOAT2"
)
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
    for f in "$ROOT"/TOOLS/*.SEQ; do names+=("$(basename "$f" .SEQ)"); done
fi
pass=0 fail=0
cd "$ROOT/TOOLS"
for n in "${names[@]}"; do
    args=()
    for p in ${NEEDS[$n]:-}; do args+=(FLOAD "$p.SEQ"); done
    if out=$(timeout 20 "$FPC" --batch -i "$IMG" - "${args[@]}" FLOAD "$n.SEQ" BYE < /dev/null 2>&1); then
        pass=$((pass+1))
    else
        fail=$((fail+1)); echo "$n: FAIL"; echo "$out" | tail -5
    fi
done
echo "$pass loaded, $fail failed"
[ $fail = 0 ]
