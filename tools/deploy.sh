#!/usr/bin/env bash
# Copy the built game to a PSP connected in USB mode.
# Usage: tools/deploy.sh [/Volumes/<psp>]
#        DEPLOY_BUILD=build/psp-foo tools/deploy.sh   # another build dir (default build/psp)
# Without an argument it waits (up to 2 min) for a memory stick with a PSP/ folder.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/${DEPLOY_BUILD:-build/psp}/FuryOfTheSky"
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
# A full stick makes rsync die halfway, leaving a mix of old and new files:
# check first. Need = new build minus what the old copy already occupies.
need_kb=$(du -sk "$DIST" | cut -f1)
have_kb=$(du -sk "$TARGET" 2>/dev/null | cut -f1 || echo 0)
free_kb=$(df -k "$STICK" | awk 'NR==2 {print $4}')
if (( need_kb - ${have_kb:-0} + 512 > free_kb )); then
    echo "Not enough space on the memory stick: need ~$(( (need_kb - ${have_kb:-0} + 512) / 1024 + 1 )) MB more," \
         "free $(( free_kb / 1024 )) MB. Free some space (e.g. an ISO) and run again."
    exit 1
fi
mkdir -p "$TARGET"
# --delete must not take what the game writes on the stick itself: the pilot's
# profile (docs/formats/profile.md) and the screenshots.
rsync -a --delete --exclude '._*' --exclude 'profile.*' --exclude 'screenshots/' "$DIST/" "$TARGET/"
# macOS sprinkles AppleDouble files that show up as broken entries in the XMB.
dot_clean -m "$STICK/PSP/GAME" 2>/dev/null || true
sync
diskutil eject "$STICK" >/dev/null 2>&1 && echo "Ejected." || echo "Eject it from Finder before unplugging."
echo "Done. Exit USB mode and launch from Game > Memory Stick."
