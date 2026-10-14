#!/usr/bin/env python3
"""Voice warning messages (RI-65 "Rita" wording) for Fury of the Sky.

Writes assets/sfx/ri_*.wav: 22.05 kHz mono 16-bit (docs/formats/wav.md).

The wording is the real RI-65 set (docs/decisions/0024). Real cockpit recordings
are not ours to ship, so by default each phrase is spoken by the macOS Russian
voice "Milena" (female, like the real informer) and then processed to sound like
the aircraft intercom: telephone band 300-3400 Hz, compressed, slightly driven.

To use a recording you have the rights to, drop it into tools/audio/voice_src/
as <name>.wav (or .aiff/.m4a/.mp3 - anything afconvert reads) and rerun: it goes
through the same intercom chain instead of the synthetic voice.

Usage: python3 tools/audio/voice.py
"""

import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

RATE = 22050
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "assets", "sfx")
SRC = os.path.join(HERE, "voice_src")

# Must match audio::messageFile() in src/audio/voice_informer.cpp.
MESSAGES = {
    "ri_pull_up": "Переведи в набор.",
    "ri_altitude": "Высота.",
    "ri_g_limit": "Предельная перегрузка.",
    "ri_aoa_limit": "Предельный угол атаки.",
    "ri_speed_limit": "Скорость предельная.",
}
VOICE = "Milena"
WORDS_PER_MIN = 190  # the real informer is brisk and flat, not conversational


def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1 and w.getframerate() == RATE
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0


def to_pcm(src, dst):
    subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{RATE}", "-c", "1", src, dst], check=True)


def synthesize(text, dst):
    with tempfile.TemporaryDirectory() as tmp:
        aiff = os.path.join(tmp, "say.aiff")
        subprocess.run(["say", "-v", VOICE, "-r", str(WORDS_PER_MIN), "-o", aiff, text], check=True)
        to_pcm(aiff, dst)


def band(x, lo, hi):
    f = np.fft.rfftfreq(len(x), 1.0 / RATE)
    h = 1.0 / np.sqrt(1.0 + (lo / np.maximum(f, 1e-3)) ** 4) / np.sqrt(1.0 + (f / hi) ** 8)
    return np.fft.irfft(np.fft.rfft(x) * h, len(x))


def intercom(x):
    # Trim leading/trailing silence: the message must start the instant it is due.
    level = np.abs(x)
    on = np.nonzero(level > 0.02 * level.max())[0]
    x = x[max(on[0] - int(0.01 * RATE), 0): on[-1] + int(0.05 * RATE)]
    x = band(x, 300.0, 3400.0)
    # Compressor: RMS envelope (10 ms), 3:1 above -24 dBFS of the peak.
    win = int(0.01 * RATE)
    rms = np.sqrt(np.convolve(x * x, np.ones(win) / win, mode="same")) + 1e-9
    ref = rms.max()
    db = 20 * np.log10(rms / ref)
    gain_db = np.where(db > -24.0, (db + 24.0) * (1.0 / 3.0 - 1.0), 0.0)
    x = x * 10 ** (gain_db / 20.0)
    x /= np.max(np.abs(x))
    x = np.tanh(1.8 * x) / np.tanh(1.8)  # a little headset grit
    fade = int(0.004 * RATE)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return x * 0.9


def main():
    os.makedirs(OUT, exist_ok=True)
    print(f"Writing {OUT} (voice: {VOICE}, recordings from {SRC} take precedence)")
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in MESSAGES.items():
            raw = os.path.join(tmp, name + ".wav")
            own = next((os.path.join(SRC, f) for f in sorted(os.listdir(SRC))
                        if os.path.splitext(f)[0] == name), None) if os.path.isdir(SRC) else None
            if own:
                to_pcm(own, raw)
            else:
                synthesize(text, raw)
            x = intercom(read_wav(raw))
            pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
            with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(pcm.tobytes())
            print(f"  {name + '.wav':20s} {len(pcm) / RATE:4.2f} s  {'recording' if own else 'synthetic'}"
                  f"  \"{text}\"")
    return 0


if __name__ == "__main__":
    sys.exit(main())
