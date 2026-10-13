#!/usr/bin/env bash
# One-shot PSP toolchain setup for macOS (Apple Silicon): downloads pspdev
# (resuming any partial download), installs it to ~/pspdev, wires up ~/.zshrc,
# then builds the game. Safe to re-run.
set -euo pipefail

URL="https://github.com/pspdev/pspdev/releases/download/latest/pspdev-macos-latest-arm64.tar.gz"
DL_DIR="$HOME/pspdev-download"
ARCHIVE="$DL_DIR/pspdev-macos-latest-arm64.tar.gz"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

step "Homebrew dependencies"
brew install pkgconf gnu-sed bash openssl libtool libarchive gettext texinfo bison flex isl gsl gmp mpfr libmpc libusb >/dev/null

mkdir -p "$DL_DIR"
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    step "Downloading pspdev (attempt $attempt, resumes where it stopped)"
    curl -L -C - --retry 20 --retry-delay 5 --retry-all-errors -o "$ARCHIVE" "$URL" || true
    if gzip -t "$ARCHIVE" 2>/dev/null; then
        break
    fi
    echo "Archive incomplete or corrupt, retrying..."
    sleep 3
done
gzip -t "$ARCHIVE" || { echo "Download failed. Re-run this script (it will resume)."; exit 1; }

step "Installing to ~/pspdev"
rm -rf "$HOME/pspdev"
tar -xzf "$ARCHIVE" -C "$HOME"

step "Environment (~/.zshrc)"
if ! grep -q 'export PSPDEV=' "$HOME/.zshrc" 2>/dev/null; then
    printf '\n# PSP toolchain\nexport PSPDEV="$HOME/pspdev"\nexport PATH="$PATH:$PSPDEV/bin"\n' >> "$HOME/.zshrc"
fi
export PSPDEV="$HOME/pspdev"
export PATH="$PATH:$PSPDEV/bin"
psp-gcc --version | head -1

step "Building Fury of the Sky"
cd "$ROOT"
psp-cmake -B build/psp
cmake --build build/psp

step "Done"
echo "Memory-stick folder: $ROOT/build/psp/FuryOfTheSky  ->  copy into PSP/GAME/"
echo "Or connect the PSP in USB mode and run: tools/deploy.sh"
echo "Open a new terminal (or 'source ~/.zshrc') to use psp-gcc directly."
