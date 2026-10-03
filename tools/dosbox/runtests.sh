#!/usr/bin/env bash
# Run Forth regression tests under the original F-PC in a headless DOSBox and
# compare (or record) their console output.
#
# usage: tools/dosbox/runtests.sh [--record] [--host KERNEL|F-PC] [test.seq ...]
#        default: all tests/*.seq, host KERNEL (KERNEL.COM)
#        env: TIMEOUT (seconds per batch, default 120), WORK (scratch dir),
#             ORIG (git rev of the original sources, default fpc-3.6-original)
#
# Each test runs in a fresh F-PC process. Its console output is copied to a
# log file with the PRINTING mechanism and compared against
# tests/golden/<name>.<host>.out (CRs stripped). Test file names must be valid
# DOS 8.3 names, and the files are converted to CRLF. A test needs no BYE.
# A load error is logged as "*** ERROR at line N: <word> <message>" and ends
# the test; as a backstop KEY is BYE, so anything that waits for input exits.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
record=0 host=KERNEL
while [ $# -gt 0 ]; do
    case $1 in
        --record) record=1; shift ;;
        --host)   host=$2; shift 2 ;;
        *) break ;;
    esac
done
case $host in KERNEL) exe=KERNEL ;; F-PC) exe=F-PC ;; *) echo "bad host $host"; exit 2 ;; esac
tests=("$@"); [ ${#tests[@]} -eq 0 ] && tests=("$ROOT"/tests/*.seq)

WORK=${WORK:-$(mktemp -d)}
TIMEOUT=${TIMEOUT:-120}
FPC=$WORK/dos/FPC
rm -rf "$FPC"; mkdir -p "$FPC"
git -C "$ROOT" archive "${ORIG:-fpc-3.6-original}" | tar -xf - -C "$FPC"

crlf() { printf '%s\r\n' "$@"; }
crlf ": LOGTO  ( | name -- )" \
     "        PRNHNDL HCLOSE DROP  PRNHNDL CLR-HCB" \
     "        BL WORD COUNT PRNHNDL \">HANDLE" \
     "        PRNHNDL HCREATE ABORT\" LOGTO: create failed\"" \
     "        ['] TRUE IS ?PRINTER.READY  PRINTING ON ;" \
     ": LOGERR  ( a1 n1 -- )   \ load error: log it, then leave" \
     "        PRINTING ON  CR .\" *** ERROR at line \" LOADLINE @ U." \
     "        .\" : \" HERE COUNT TYPE SPACE TYPE CR  PRINTING OFF BYE ;" \
     "' LOGERR IS DOERROR" > "$FPC/LOGTO.SEQ"

: > "$FPC/RUN.BAT"
for t in "${tests[@]}"; do
    n=$(basename "$t" .seq | tr a-z A-Z)
    sed 's/\r*$/\r/' "$t" > "$FPC/$n.SEQ"   # F-PC wants CRLF source
    # driver: hook output, run the test, stop logging, leave
    crlf "' BYE IS KEY  FLOAD LOGTO.SEQ  LOGTO $n.LOG" \
         "FLOAD $n.SEQ" \
         "PRINTING OFF  BYE" > "$FPC/$n.DRV"
    crlf "$exe - FLOAD $n.DRV" >> "$FPC/RUN.BAT"
done
crlf "EXIT" >> "$FPC/RUN.BAT"

cat > "$WORK/dosbox.conf" <<CONF
[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
cycles=fixed 6000
[autoexec]
mount c "$WORK/dos"
c:
cd \\FPC
RUN.BAT
CONF

rc=0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "$TIMEOUT" dosbox -conf "$WORK/dosbox.conf" -noconsole -exit \
    >"$WORK/dosbox.out" 2>&1 || rc=$?
[ $rc -eq 124 ] && echo "warning: DOSBox timed out"

pass=0 fail=0
for t in "${tests[@]}"; do
    base=$(basename "$t" .seq) n=$(echo "$base" | tr a-z A-Z)
    golden=$ROOT/tests/golden/$base.$host.out
    out=$WORK/$base.out
    if [ -f "$FPC/$n.LOG" ]; then tr -d '\r' < "$FPC/$n.LOG" > "$out"; else : > "$out"; echo "$base: no output"; fi
    if [ $record = 1 ]; then
        cp "$out" "$golden"; echo "$base: recorded ($(wc -l < "$out") lines)"
    elif diff -u "$golden" "$out" > "$WORK/$base.diff"; then
        pass=$((pass+1)); echo "$base: ok"
    else
        fail=$((fail+1)); echo "$base: FAIL"; head -20 "$WORK/$base.diff"
    fi
done
[ $record = 1 ] || echo "$pass passed, $fail failed  (work dir: $WORK)"
[ $fail = 0 ]
