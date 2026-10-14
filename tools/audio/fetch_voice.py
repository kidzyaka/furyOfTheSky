#!/usr/bin/env python3
"""Downloads the real RI-65 "Rita" recordings listed in voice.py into
tools/audio/voice_src/<id>.ogg (docs/decisions/0025).

Source: UwUpad, a user-uploaded soundboard (set uploaded by "NexP", sound ids
28096-28134). Provenance and rights are unknown: personal use only - the
files are kept out of git (.gitignore) and the game is not distributed.

Usage: python3 tools/audio/fetch_voice.py
"""

import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from voice import PHRASES, SRC  # noqa: E402

UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 Safari/605.1.15"


def main():
    os.makedirs(SRC, exist_ok=True)
    for name, text, uid, _ in PHRASES:
        if uid is None:
            continue
        dst = os.path.join(SRC, f"{uid}.ogg")
        if os.path.exists(dst) and os.path.getsize(dst) > 0:
            continue
        req = urllib.request.Request(f"https://cdn.uwupad.me/{uid}.ogg",
                                     headers={"User-Agent": UA, "Referer": f"https://uwupad.me/sound/{uid}"})
        with urllib.request.urlopen(req, timeout=30) as r, open(dst, "wb") as f:
            f.write(r.read())
        print(f"  {uid}.ogg  {os.path.getsize(dst) // 1024:3d} KB  {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
