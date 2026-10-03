#!/usr/bin/env bash
# Rebuild KERNEL.COM (metacompile) and F-PC.EXE (extend) from the original
# F-PC sources in a headless DOSBox, and compare against the shipped binaries.
#
# usage: tools/dosbox/build.sh [workdir]
#        env: TIMEOUT (seconds, default 300), CYCLES (default 6000)
#
# The original tree (git tag fpc-3.6-original, or $ORIG) is copied to
# <workdir>/dos/FPC and mounted as C:\FPC (the FPATH in F-PC.CFG / KERNEL.CFG
# expects that location). Sources are not modified. Results land in <workdir>/dos/FPC: KERNEL.COM, F-PC.EXE, META.LOG.
#
# Gotchas this script works around:
#  - A DOS command tail is limited to 127 characters; a longer F-PC command
#    line is silently truncated and loses its final BYE (DOSBox then hangs).
#  - .COMPSTAT's lines/minute calculation overflows 16-bit */ (divide trap)
#    above ~300k lines/min. Stage 1 stubs it out; stage 2 can't (it's defined
#    inside F-PC.SEQ), so the emulated CPU runs at a fixed, modest speed.
#  - SAVE-EXE forces a .EXE extension.
#  - Stage 2 runs exactly as EXTEND.BAT does (minus ".s"): anything loaded or
#    patched first (a log hook, a KEY stub, a driver file's FILES entry)
#    would end up in the saved image. So there is no EXTEND log.
#
# Expected result: KERNEL.COM is byte-identical. F-PC.EXE is 288 bytes
# smaller than the shipped one, because this tree's VALIDATE.SEQ has its
# body commented out with { } (the shipped binary still contains
# .VALIDATE / UNVALIDATE / %UNVALIDATE); the word lists are otherwise equal.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${1:-$(mktemp -d)}
TIMEOUT=${TIMEOUT:-300}
FPC=$WORK/dos/FPC

rm -rf "$FPC"; mkdir -p "$FPC"
git -C "$ROOT" archive "${ORIG:-fpc-3.6-original}" | tar -xf - -C "$FPC"
mv "$FPC/KERNEL.COM" "$FPC/KERNEL.ORI"
cp "$FPC/F-PC.EXE"   "$FPC/F-PC.ORI"   # stage 1 runs on the shipped F-PC.EXE

crlf() { printf '%s\r\n' "$@"; }
# LOGTO <file>: send PRINTING output to a file. Works in the bare kernel,
# which lacks PRINT.SEQ's PFILE.
crlf ": LOGTO  ( | name -- )" \
     "        PRNHNDL HCLOSE DROP  PRNHNDL CLR-HCB" \
     "        BL WORD COUNT PRNHNDL \">HANDLE" \
     "        PRNHNDL HCREATE ABORT\" LOGTO: create failed\"" \
     "        ['] TRUE IS ?PRINTER.READY  PRINTING ON ;" > "$FPC/LOGTO.SEQ"
# ' BL IS KEY answers every prompt (e.g. "print LABELS Y/N") with N.
crlf "AUTOEDITOFF HWORDS-  ' BL IS KEY  : .COMPSTAT ;" \
     "FLOAD LOGTO.SEQ  LOGTO META.LOG" \
     "FLOAD META86.SEQ" \
     "PRINTING OFF BYE" > "$FPC/STAGE1.SEQ"
crlf "F-PC - FLOAD STAGE1.SEQ" \
     "DEL F-PC.EXE" \
     "KERNEL - FLOAD F-PC.SEQ SAVE-EXE F-PC.EXE BYE" \
     "EXIT" > "$FPC/BUILD.BAT"

cat > "$WORK/dosbox.conf" <<CONF
[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
cycles=fixed ${CYCLES:-6000}
[autoexec]
mount c "$WORK/dos"
c:
cd \\FPC
BUILD.BAT
CONF

echo "building in $WORK (timeout ${TIMEOUT}s)..."
rc=0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "$TIMEOUT" dosbox -conf "$WORK/dosbox.conf" -noconsole -exit \
    >"$WORK/dosbox.out" 2>&1 || rc=$?
[ $rc -eq 124 ] && echo "warning: DOSBox timed out (F-PC probably stuck at a prompt)"

status=0
for pair in KERNEL.COM:KERNEL.ORI F-PC.EXE:F-PC.ORI; do
    new=$FPC/${pair%%:*} ori=$FPC/${pair##*:}
    if [ ! -f "$new" ]; then
        echo "${pair%%:*}: NOT BUILT"; status=1
    elif cmp -s "$new" "$ori"; then
        echo "${pair%%:*}: identical to shipped binary"
    else
        echo "${pair%%:*}: differs (size $(stat -c%s "$new") vs shipped $(stat -c%s "$ori"))"
        status=1
    fi
done
exit $status
