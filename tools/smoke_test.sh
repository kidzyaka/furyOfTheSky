#!/usr/bin/env bash
# Emulator smoke test (dev Mac only, see docs/psp/emulator.md).
# Runs the built game from PPSSPP's own memory stick with the `autoshot` hook:
# the game flies a scripted sortie through every effect (8 shots: cruise, AB turn
# with vapor, flares, cobra, vapor cone, sonic boom, low pass, crash), visits the
# hangar (1 shot), quits. Fails on unresolved imports.
# Output: build/smoke/shot_*.png and build/smoke/ppsspp.log
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/build/psp/FuryOfTheSky"
OUT="$ROOT/build/smoke"
PPSSPP=/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
CFG="$HOME/.config/ppsspp/PSP"
MS="$CFG/GAME/FuryOfTheSky"

[[ -x "$PPSSPP" ]] || { echo "PPSSPP missing: brew install --cask ppsspp"; exit 1; }
[[ -f "$DIST/EBOOT.PBP" ]] || { echo "Not built: cmake --build build/psp"; exit 1; }

# Static check first: a user-mode module must not import kernel libraries.
# (Captured into a variable: `strings | grep -q` under pipefail reports failure
# when grep exits early and strings dies of SIGPIPE.)
kernel_imports="$(strings "$ROOT/build/psp/fury" | grep 'ForKernel' | sort -u || true)"
if [[ -n "$kernel_imports" ]]; then
    echo "FAIL: kernel-mode imports in the ELF (is pspkernel linked?)"
    echo "$kernel_imports"
    exit 1
fi

# CPU reads of VRAM (our screenshots) are only correct with the software renderer.
INI="$CFG/SYSTEM/ppsspp.ini"
[[ -f "$INI" ]] && sed -i '' 's/^SoftwareRenderer = False/SoftwareRenderer = True/' "$INI"

mkdir -p "$MS" "$OUT"
rsync -a --delete "$DIST/" "$MS/"
touch "$MS/autoshot"
rm -f "$OUT"/shot_*.png "$OUT/ppsspp.log"   # PPSSPP appends to an existing log

"$PPSSPP" --log="$OUT/ppsspp.log" -d "$MS/EBOOT.PBP" >/dev/null 2>&1 &
PID=$!
EXPECTED=9
LAST="$MS/screenshots/shot_$(printf %03d $((EXPECTED - 1))).bmp"
for _ in $(seq 1 240); do
    sleep 1
    [[ -f "$LAST" ]] && break
done
sleep 2
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

status=0
if grep -qE 'Unknown syscall|bad syscall' "$OUT/ppsspp.log"; then
    echo "FAIL: unresolved syscalls:"
    grep -E 'Unknown syscall|Importing \[UNK' "$OUT/ppsspp.log" | sort -u | head
    status=1
fi
shots=0
for bmp in "$MS"/screenshots/shot_*.bmp; do
    [[ -f "$bmp" ]] || continue
    sips -s format png "$bmp" --out "$OUT/$(basename "${bmp%.bmp}").png" >/dev/null
    shots=$((shots + 1))
done
rm -rf "$MS/screenshots"
if (( shots < EXPECTED )); then
    echo "FAIL: expected $EXPECTED screenshots, got $shots (game hung or crashed? see $OUT/ppsspp.log)"
    status=1
fi
(( status == 0 )) && echo "PASS: $shots screenshots in $OUT"
exit $status
