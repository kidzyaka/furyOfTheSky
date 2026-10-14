#!/usr/bin/env python3
"""Procedural sound effects for Fury of the Sky (docs/decisions/0024).

Writes assets/sfx/*.wav: 22.05 kHz mono 16-bit (docs/formats/wav.md).

Loops are synthesised in the frequency domain over exactly the loop length:
random phases + a designed magnitude spectrum, then an inverse FFT. The result
is periodic by construction, so the loop point is seamless with no crossfade.
Amplitude modulation uses periodic noise built the same way.

Physics behind the shapes (sources in the ADR):
  * jet mixing noise - broadband hump, peak ~100-300 Hz for a large engine
    heard from behind, falling off ~2 octaves each side;
  * turbine/fan tones - narrow peaks at blade-passing frequencies and, with a
    supersonic fan tip, "buzz-saw" tones at multiples of the shaft rate;
  * afterburner crackle - not merely skewed noise but shock-like events:
    near-instant pressure rises with slow decay (high dp/dt);
  * buffet - low-frequency shaking with a rattle the PSP's small speakers can
    actually reproduce (they roll off below a few hundred Hz).

Usage: python3 tools/audio/sfx.py
"""

import os
import sys
import wave

import numpy as np

RATE = 22050
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "sfx")

rng = np.random.default_rng(0x5135)


def write(name, x, peak=0.89, rms=None):
    """Normalise (by RMS for loops, so code gains compare like for like; by peak
    for one-shots) and store as 16-bit PCM."""
    x = np.asarray(x, dtype=np.float64)
    x = x - x.mean()
    if rms is not None:
        x *= rms / max(np.sqrt(np.mean(x * x)), 1e-9)
        p = np.max(np.abs(x))
        if p > 0.97:  # soft-limit the rare peaks rather than lower the whole loop
            x = np.tanh(x / 0.97) * 0.97
    else:
        x *= peak / max(np.max(np.abs(x)), 1e-9)
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print(f"  {name + '.wav':20s} {len(pcm) / RATE:5.2f} s  {len(pcm) * 2 // 1024:4d} KB")


def freqs(n):
    return np.fft.rfftfreq(n, 1.0 / RATE)


def shaped_noise(n, magnitude):
    """Periodic noise of length n with |spectrum| = magnitude(f)."""
    f = freqs(n)
    mag = magnitude(f)
    mag[0] = 0.0
    phase = rng.uniform(0, 2 * np.pi, len(f))
    return np.fft.irfft(mag * np.exp(1j * phase), n)


def log_hump(fp, low_oct, high_oct):
    """Spectral hump peaking at fp, widths in octaves on each side - in energy
    per octave, like the 1/3-octave spectra acoustics papers plot. (FFT bins are
    evenly spaced in Hz, so a hump drawn per bin would put most of the energy in
    the many high-frequency bins and sound far too bright: hence the 1/sqrt(f).)"""
    def m(f):
        f = np.maximum(f, 1.0)
        o = np.log2(f / fp)
        w = np.where(o < 0, low_oct, high_oct)
        return np.exp(-0.5 * (o / w) ** 2) / np.sqrt(f / fp)
    return m


def highpass(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (fc / np.maximum(f, 1e-3)) ** (2 * order))


def lowpass(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (f / fc) ** (2 * order))


def slow_mod(n, fmax, depth):
    """Periodic random envelope around 1 with content below fmax Hz."""
    m = shaped_noise(n, lambda f: (f > 0) * (f < fmax) * 1.0)
    m /= max(np.max(np.abs(m)), 1e-9)
    return 1.0 + depth * m


def peaks(centres, widths, gains):
    def m(f):
        out = np.zeros_like(f)
        for c, w, g in zip(centres, widths, gains):
            out += g * np.exp(-0.5 * ((f - c) / w) ** 2)
        return out
    return m


def env(n, attack, decay):
    t = np.arange(n) / RATE
    return (1 - np.exp(-t / max(attack, 1e-4))) * np.exp(-t / decay)


# --- Loops -------------------------------------------------------------------

def engine_roar():
    n = int(3.0 * RATE)
    # Jet mixing noise behind a ~14 t-thrust turbofan: peak ~170 Hz, a long
    # high-frequency skirt (it is what small speakers reproduce).
    hump = log_hump(170.0, 1.3, 2.4)
    x = shaped_noise(n, lambda f: hump(f) * highpass(35.0)(f))
    # Large turbulent eddies: slow, irregular swell of the roar.
    x *= slow_mod(n, 4.0, 0.18)
    write("engine_roar", x, rms=0.16)


def engine_whine():
    n = int(2.0 * RATE)
    # Fan blade-passing tone and harmonics, a buzz-saw series at shaft orders,
    # a faint high-pressure compressor tone. Narrow noise peaks rather than
    # pure sines: real tones wander, which keeps the loop from sounding synthetic.
    shaft = 145.0
    centres, widths, gains = [], [], []
    for k in range(3, 13):  # buzz-saw: shaft orders, uneven levels
        centres.append(shaft * k)
        widths.append(4.0)
        gains.append(rng.uniform(0.15, 0.5) * np.exp(-((k - 7) / 4.0) ** 2))
    for h, g in ((1, 1.0), (2, 0.45), (3, 0.18)):  # fan BPF ~1.16 kHz
        centres.append(1160.0 * h)
        widths.append(6.0 * h)
        gains.append(g)
    centres.append(4350.0)  # HP compressor
    widths.append(12.0)
    gains.append(0.22)
    tones = shaped_noise(n, peaks(centres, widths, gains))
    hiss = shaped_noise(n, log_hump(2500.0, 1.2, 1.0)) * 0.25
    x = tones / np.sqrt(np.mean(tones ** 2)) + hiss / np.sqrt(np.mean(hiss ** 2)) * 0.3
    write("engine_whine", x, rms=0.12)


def afterburner():
    n = int(3.0 * RATE)
    # Lower, broader, much louder hump than dry power ...
    hump = log_hump(110.0, 1.4, 2.8)
    roar = shaped_noise(n, lambda f: hump(f) * highpass(30.0)(f))
    roar *= slow_mod(n, 9.0, 0.25)  # combustion roughness
    roar /= np.sqrt(np.mean(roar ** 2))
    # ... plus crackle: shock-like events, near-instant rise, ~1.5 ms decay.
    crackle = np.zeros(n)
    count = int(3.0 * 55)  # ~55 events/s
    pos = rng.integers(0, n, count)
    amp = rng.pareto(2.5, count) + 0.3  # heavy tail: a few loud cracks
    k = np.arange(int(0.008 * RATE))
    shape = np.exp(-k / (0.0015 * RATE)) - 0.35 * np.exp(-k / (0.004 * RATE))
    for p, a in zip(pos, amp):
        idx = (p + k) % n  # wrap: the loop stays seamless
        crackle[idx] += a * shape
    crackle = np.fft.irfft(np.fft.rfft(crackle) * highpass(400.0)(freqs(n)), n)
    crackle /= np.sqrt(np.mean(crackle ** 2))
    write("afterburner", roar + 0.55 * crackle, rms=0.17)


def wind():
    n = int(2.5 * RATE)
    # Turbulent boundary layer and canopy/edge noise: broad, centred ~900 Hz.
    x = shaped_noise(n, lambda f: log_hump(900.0, 1.8, 1.6)(f) * highpass(120.0)(f))
    x *= slow_mod(n, 2.5, 0.22)  # gusts
    write("wind", x, rms=0.13)


def buffet():
    n = int(2.0 * RATE)
    shake = slow_mod(n, 14.0, 0.9)  # 4-14 Hz airframe shaking
    shake = np.maximum(shake, 0.0)
    low = shaped_noise(n, lambda f: log_hump(45.0, 0.8, 1.2)(f) * highpass(12.0)(f))
    rattle = shaped_noise(n, lambda f: log_hump(420.0, 0.8, 1.0)(f))
    low /= np.sqrt(np.mean(low ** 2))
    rattle /= np.sqrt(np.mean(rattle ** 2))
    write("buffet", (low + 0.6 * rattle) * shake, rms=0.15)


# --- One-shots -----------------------------------------------------------------

def ab_light():
    n = int(1.1 * RATE)
    t = np.arange(n) / RATE
    # The "whump" of the afterburner lighting: a pressure pulse sweeping down,
    # with a burst of combustion noise; mid content so speakers carry it.
    f = 95.0 * np.exp(-t / 0.25) + 38.0
    thump = np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(n, 0.004, 0.22)
    burst = shaped_noise(n, lambda fr: log_hump(320.0, 1.2, 1.6)(fr)) * env(n, 0.01, 0.35)
    burst /= np.max(np.abs(burst))
    write("ab_light", thump + 0.7 * burst, peak=0.9)


def flare():
    n = int(0.6 * RATE)
    # Cartridge squib: sharp broadband crack, then the pellet's pyrotechnic hiss.
    crack = shaped_noise(n, lambda f: highpass(300.0)(f)) * env(n, 0.0003, 0.006)
    hiss = shaped_noise(n, lambda f: log_hump(3200.0, 1.0, 1.0)(f)) * env(n, 0.02, 0.18)
    crack /= np.max(np.abs(crack))
    hiss /= np.max(np.abs(hiss))
    write("flare", crack + 0.35 * hiss, peak=0.85)


def explosion():
    n = int(3.2 * RATE)
    crack = shaped_noise(n, lambda f: highpass(150.0)(f)) * env(n, 0.0005, 0.02)
    body = shaped_noise(n, lambda f: log_hump(160.0, 1.4, 1.8)(f)) * env(n, 0.006, 0.6)
    rumble = shaped_noise(n, lambda f: log_hump(60.0, 1.0, 1.4)(f)) * env(n, 0.05, 1.4)
    debris = np.zeros(n)  # crackling debris, thinning out
    for p in rng.integers(int(0.05 * RATE), int(1.6 * RATE), 140):
        k = np.arange(int(0.004 * RATE))
        if p + len(k) < n:
            debris[p + k] += rng.uniform(0.2, 1.0) * np.exp(-p / (0.6 * RATE)) * np.exp(-k / 20.0)
    debris = np.fft.irfft(np.fft.rfft(debris) * highpass(800.0)(freqs(n)), n)
    parts = [crack, body, rumble, debris]
    parts = [p / np.max(np.abs(p)) for p in parts]
    x = parts[0] * 0.8 + parts[1] + parts[2] * 0.8 + parts[3] * 0.25
    write("explosion", np.tanh(x * 1.4), peak=0.92)


def ui_move():
    n = int(0.045 * RATE)
    t = np.arange(n) / RATE
    write("ui_move", np.sin(2 * np.pi * 1650.0 * t) * env(n, 0.002, 0.012), peak=0.5)


def ui_select():
    n = int(0.12 * RATE)
    t = np.arange(n) / RATE
    f = np.where(t < 0.05, 1250.0, 1870.0)
    write("ui_select", np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(n, 0.002, 0.05), peak=0.55)


def main():
    os.makedirs(OUT, exist_ok=True)
    print(f"Writing {OUT}")
    for make in (engine_roar, engine_whine, afterburner, wind, buffet,
                 ab_light, flare, explosion, ui_move, ui_select):
        make()
    return 0


if __name__ == "__main__":
    sys.exit(main())
