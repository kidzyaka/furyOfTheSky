#!/usr/bin/env bash
# Copy the built game to a PSP connected in USB mode.
# Usage: tools/deploy.sh [/Volumes/<psp>]
# Without an argument it waits (up to 2 min) for a memory stick with a PSP/ folder.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/build/psp/FuryOfTheSky"
[[ -f "$DIST/EBOOT.PBP" ]] || { echo "Not built yet: run  psp-cmake -B build/psp && cmake --build build/psp"; exit 1; }

find_stick() {
    for v in /Volumes/*; do
        [[ -d "$v/PSP" ]] && { echo "$v"; return 0; }
    done
    return 1
}

STICK="${1:-}"
if [[ -z "$STICK" ]]; then
    echo "Waiting for the PSP... (cable in, then on the PSP: Settings > USB Connection)"
    for _ in $(seq 1 120); do
        STICK="$(find_stick || true)"
        [[ -n "$STICK" ]] && break
        sleep 1
    done
fi
[[ -n "$STICK" ]] || {
    echo "PSP not found. Check: data-capable mini-USB cable, PSP in USB mode, memory stick inserted."
    exit 1
}

TARGET="$STICK/PSP/GAME/FuryOfTheSky"
echo "Deploying to $TARGET"
mkdir -p "$TARGET"
rsync -a --delete --exclude '._*' "$DIST/" "$TARGET/"
# macOS sprinkles AppleDouble files that show up as broken entries in the XMB.
dot_clean -m "$STICK/PSP/GAME" 2>/dev/null || true
sync
diskutil eject "$STICK" >/dev/null 2>&1 && echo "Ejected." || echo "Eject it from Finder before unplugging."
echo "Done. Exit USB mode and launch from Game > Memory Stick."
