#!/usr/bin/env bash
# Emulator smoke test (dev Mac only, see docs/psp/emulator.md).
# Runs the built game from PPSSPP's own memory stick with the `autoshot` hook:
# 1. the menu tour (21 shots, kTour in src/scenes/menu/menu_scene.cpp): main
#    menu, SINGLEPLAYER drawer (+ cursor on TRAINING), training, campaign
#    carousel (+ slid), location, briefing pages x4, hangar, shop, profile,
#    callsign keyboard, emblem gallery, settings, multiplayer, lobbies,
#    debriefing of a won and of a lost mission;
# 2. a scripted sortie through every effect and the weapons (27 shots: cruise,
#    AB turn with vapor, flares, cobra, transonic cloud, supersonic, R-73 locked,
#    launch, smoke trail, the hit, R-77 ejected, gun burst, a HARD bot's radar
#    lock on the RWR, its missile warning, low pass, crash; then the weathers:
#    cloudy dawn, under / above the overcast, storm at dusk, snow, low over the
#    storm's waves; then the map: the Gates' stacks, a pass at 8 m over the
#    sea, the western islet; two patrol ships ahead, then firing at us), then quits.
# Fails on unresolved imports.
#
#   SMOKE_BUILD=build/psp-foo tools/smoke_test.sh   # another build dir (default build/psp)
#   SMOKE_EXPECTED=16 ...                           # expected screenshot count (default 54)
# Output: build/smoke[-foo]/shot_*.png and ppsspp.log. Runs are serialised by a
# lock (build/.smoke_lock): PPSSPP's memory stick folder is shared.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${SMOKE_BUILD:-build/psp}"
DIST="$ROOT/$BUILD/FuryOfTheSky"
SUFFIX="${BUILD#build/psp}"
OUT="$ROOT/build/smoke${SUFFIX}"
PPSSPP=/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
# Silent by default: the author asked not to have the emulator blaring (SDL dummy driver).
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG="$HOME/.config/ppsspp/PSP"
MS="$CFG/GAME/FuryOfTheSky"

[[ -x "$PPSSPP" ]] || { echo "PPSSPP missing: brew install --cask ppsspp"; exit 1; }
[[ -f "$DIST/EBOOT.PBP" ]] || { echo "Not built: cmake --build $BUILD"; exit 1; }

LOCK="$ROOT/build/.smoke_lock"
mkdir -p "$ROOT/build"
for _ in $(seq 1 900); do
    mkdir "$LOCK" 2>/dev/null && break
    sleep 1
done
[[ -d "$LOCK" ]] || { echo "Could not take $LOCK"; exit 1; }
trap 'rm -rf "$LOCK"' EXIT

# Static check first: a user-mode module must not import kernel libraries.
# (Captured into a variable: `strings | grep -q` under pipefail reports failure
# when grep exits early and strings dies of SIGPIPE.)
kernel_imports="$(strings "$ROOT/$BUILD/fury" | grep 'ForKernel' | sort -u || true)"
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
EXPECTED=${SMOKE_EXPECTED:-54}  # 21 menu tour + 33 sortie
LAST="$MS/screenshots/shot_$(printf %03d $((EXPECTED - 1))).bmp"
for _ in $(seq 1 480); do
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
