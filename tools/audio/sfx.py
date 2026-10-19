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
  * weapons - blast waves (Friedlander pulses) sized by charge and distance, air
    absorption, a receding missile with its Doppler and 1/r computed from its
    acceleration; reasoning and sources in docs/assets/sound.md.
  * menu - metal switchgear: impacts driving modal resonators of real-sized
    parts; sources in docs/assets/sound.md, "Интерфейс (меню)".

Usage: python3 tools/audio/sfx.py        every sound
       python3 tools/audio/sfx.py ui     menu sounds only, a check table and
                                         build/ui_sounds_preview.wav
"""

import os
import sys
import wave

import numpy as np

RATE = 22050
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "sfx")

rng = np.random.default_rng(0x5135)


def write(name, x, peak=0.89, rms=None, scale=None):
    """Normalise (by RMS for loops, so code gains compare like for like; by peak
    for one-shots; or by a fixed `scale` shared by sounds that must match, like
    the gun loop and its tail) and store as 16-bit PCM."""
    x = np.asarray(x, dtype=np.float64)
    x = x - x.mean()
    if scale is not None:
        x = x * scale
    elif rms is not None:
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


# --- Weapons (docs/assets/sound.md, "Оружие") ------------------------------------

C_SOUND = 340.0      # m/s
V_FLIGHT = 250.0     # m/s, typical combat speed for the Doppler of a receding missile
GUN_PERIOD = 882     # samples: 40 ms at 22 050 Hz = 1500 rounds/min, the GSh-30-1 setting


def air_absorption_db_per_km(f):
    """Rough fit to ISO 9613-1 at ~20 C, 50-70 % RH: ~1 dB/km at 250 Hz,
    ~6 at 1 kHz, ~15 at 2 kHz, ~37 at 4 kHz, ~90 at 8 kHz."""
    return 1.0 * (np.maximum(f, 1.0) / 250.0) ** 1.3


def absorbed(dist_m):
    return lambda f: 10.0 ** (-air_absorption_db_per_km(f) * dist_m / 1000.0 / 20.0)


def friedlander(t_pos, b=1.6, length=8.0):
    """Blast-wave pressure: instant rise, positive phase of t_pos seconds, then the
    shallower negative phase. Its spectrum peaks near 0.3 / t_pos."""
    t = np.arange(int(length * t_pos * RATE)) / RATE
    return (1.0 - t / t_pos) * np.exp(-b * t / t_pos)


def filt(x, magnitude):
    """Zero-phase FFT filter. Circular, so on a loop it keeps the loop seamless."""
    return np.fft.irfft(np.fft.rfft(x) * magnitude(freqs(len(x))), len(x))


def burst_noise(length, magnitude):
    """Non-periodic noise burst of `length` samples with |spectrum| ~ magnitude."""
    return filt(rng.standard_normal(length), magnitude)


def modes(n, partials, start=0, phase=None):
    """Damped inharmonic ringing (struck panel, piston on its stop):
    partials = [(Hz, decay s, gain)]. Random phases from `rng` unless `phase` is
    given (0 = a struck mode starting from rest; draws nothing from `rng`)."""
    t = np.arange(n - start) / RATE
    out = np.zeros(n)
    for f, d, g in partials:
        p = rng.uniform(0, 2 * np.pi) if phase is None else phase
        out[start:] += g * np.sin(2 * np.pi * f * t + p) * np.exp(-t / d)
    return out


def add_at(dst, src, pos, wrap=False):
    k = np.arange(len(src))
    if wrap:
        np.add.at(dst, (pos + k) % len(dst), src)
    else:
        k = k[pos + k < len(dst)]
        dst[pos + k] += src[k]


def gun_shot(n, pos, wrap, amp=1.0, t_pos=2.6e-3):
    """One GSh-30-1 round heard from the chase camera (~20 m behind the muzzle),
    v2 (docs/assets/sound.md: the author found v1 "weak, no power").
    - Muzzle blast: an N-like pressure pulse. A 30 mm round's propellant
      (~0.4 kg of gas at the muzzle) gives a long positive phase, ~2.5 ms:
      spectrum peak ~120 Hz - the body of the sound.
    - The blast's near-field is broadband too (shock front steepening):
      a few ms of hard noise around 1-2 kHz - what carries the "rip" on small
      speakers that cannot play 120 Hz.
    - The airframe: the gun is bolted into the LERX and its ~6 t recoil force
      rings the structure - a low thud (60 and 95 Hz modes, tens of ms),
      felt in the cockpit, heard from behind as the weight of each round.
    - The propellant gas jet: tens of ms of rough noise ~400 Hz that runs into
      the next round's - the shots merge into one continuous "brrrt".
    The supersonic shell's own crack is beamed forward and not heard here."""
    x = np.zeros(n)
    add_at(x, amp * friedlander(t_pos, b=1.3), pos, wrap)
    cl = int(0.006 * RATE)
    crack = burst_noise(cl, log_hump(1400.0, 1.3, 1.6)) * env(cl, 0.0001, 0.0018)
    add_at(x, amp * 0.75 * crack / np.max(np.abs(crack)), pos, wrap)
    m = int(0.06 * RATE)
    thud = modes(m, [(62.0, 0.030, 0.42), (96.0, 0.018, 0.30), (173.0, 0.010, 0.16)])
    thud *= env(m, 0.0008, 1.0)
    add_at(x, amp * thud, pos, wrap)
    gas_len = int(0.045 * RATE)
    gas = burst_noise(gas_len, log_hump(420.0, 1.3, 1.6)) * env(gas_len, 0.0006, 0.016)
    add_at(x, amp * 0.42 * gas / np.max(np.abs(gas)), pos, wrap)
    return x


def gun_burst_and_tail():
    # 25 rounds/s: the pulse train has a 25 Hz fundamental and a dense comb of
    # harmonics - the low "brrrt". The loop is exactly 25 periods, every pulse
    # is placed modulo the loop length: seamless by construction.
    shots = 25
    n = shots * GUN_PERIOD
    burst = np.zeros(n)
    for i in range(shots):
        jitter = 0 if i == 0 else int(rng.integers(-12, 13))  # +-0.5 ms: action timing
        amp = rng.uniform(0.9, 1.07)                            # charge-to-charge spread
        t_pos = 2.6e-3 * rng.uniform(0.9, 1.1)
        burst += gun_shot(n, i * GUN_PERIOD + jitter, True, amp, t_pos)
    # Between the rounds the gas never quite stops: a rough bed modulated at
    # the firing rate (circular noise, seamless).
    bed = shaped_noise(n, log_hump(600.0, 1.5, 1.5))
    phase = (np.arange(n) % GUN_PERIOD) / GUN_PERIOD
    bed *= 0.35 + 0.65 * np.exp(-phase * 5.0)
    burst += 0.22 * bed / np.sqrt(np.mean(bed ** 2)) * np.sqrt(np.mean(burst ** 2))
    shape = lambda f: highpass(35.0)(f) * lowpass(7000.0)(f)
    burst = filt(burst, shape)
    # Saturation as the blast's own non-linearity near the muzzle, driven hard:
    # a low crest factor is what makes a burst sound heavy at a given peak. The
    # tail goes through the same curve and scale so its round matches the loop's.
    drive = 3.0 / np.max(np.abs(burst))
    burst = np.tanh(burst * drive)
    scale = 0.92 / np.max(np.abs(burst - burst.mean()))
    write("gun_burst", burst, scale=scale)

    # Release: the last round, then its gas cloud swept back past the camera.
    m = int(0.45 * RATE)
    tail = gun_shot(m, 0, False, 1.0)
    whoosh = burst_noise(m, lambda f: log_hump(260.0, 1.2, 1.4)(f)) * env(m, 0.01, 0.09)
    tail += 0.22 * whoosh / np.max(np.abs(whoosh))
    tail = np.tanh(filt(tail, shape) * drive)
    tail *= np.minimum(1.0, (m - np.arange(m)) / (0.05 * RATE))  # end on zero
    write("gun_tail", tail, scale=scale)


def missile_launch():
    """Solid rocket motor lighting ~20 m from the camera, then receding at 14 g.
    The missile is simulated: emission time tau, distance r(tau) = r0 + a tau^2 / 2
    relative to the aircraft/camera (both keep flying at V). In the camera's frame
    the sound comes back at c + V, so a signal emitted at tau arrives at
    t = tau + (r - r0) / (c + V) and the pitch ratio is dtau/dt = 1 / (1 + a tau / (c + V)):
    ~0.7 after 2 s (it is a real, not slight, Doppler drop). Level falls as r0/r
    (-24 dB at 300 m) and the highs go first by air absorption."""
    a = 14.0 * 9.81
    r0 = 20.0
    cv = C_SOUND + V_FLIGHT
    dur = 2.5
    n_out = int(dur * RATE)
    tau_max = (-1.0 + np.sqrt(1.0 + 2.0 * (a / cv) * dur)) / (a / cv) + 0.05
    ns = int(tau_max * RATE) + 2

    # Source signal in emission time. Rocket exhaust noise: broad hump, harsh and
    # bright for a small nozzle (peak ~0.8 kHz), with strong crackle (shock-like
    # events: steepened waves of a supersonic, very hot jet).
    roar = shaped_noise(ns, lambda f: log_hump(800.0, 1.8, 2.2)(f) * highpass(60.0)(f))
    roar *= slow_mod(ns, 25.0, 0.2)  # combustion roughness
    roar /= np.sqrt(np.mean(roar ** 2))
    crackle = np.zeros(ns)
    count = int(tau_max * 140)
    k = np.arange(int(0.006 * RATE))
    shape = np.exp(-k / (0.0008 * RATE)) - 0.4 * np.exp(-k / (0.0025 * RATE))
    for p, amp in zip(rng.integers(0, ns, count), rng.pareto(2.2, count) + 0.3):
        add_at(crackle, amp * shape, int(p))
    crackle = filt(crackle, highpass(500.0))
    crackle /= np.sqrt(np.mean(crackle ** 2))
    src = roar + 0.6 * crackle
    t_s = np.arange(ns) / RATE
    src *= 1.0 - np.exp(-t_s / 0.025)  # chamber pressure rise
    # Ignition: igniter pop and the nozzle closure bursting - a short sharp crack.
    crack = friedlander(0.5e-3, length=10.0)
    add_at(src, 6.0 * crack, 0)

    # Propagation: per-band distance attenuation (absorption) + spreading.
    r = r0 + 0.5 * a * t_s ** 2
    edges = [0, 700, 1600, 3500, 7000, RATE / 2 + 1]
    centres = [300, 1050, 2400, 5000, 9000]
    spec = np.fft.rfft(src)
    f = freqs(ns)
    weighted = np.zeros(ns)
    for lo, hi, fc in zip(edges[:-1], edges[1:], centres):
        band = np.fft.irfft(spec * ((f >= lo) & (f < hi)), ns)
        weighted += band * 10.0 ** (-air_absorption_db_per_km(fc) * (r - r0) / 1000.0 / 20.0)
    weighted *= r0 / r

    # Resample from emission time to arrival time (the Doppler drop).
    t = np.arange(n_out) / RATE
    tau = (-1.0 + np.sqrt(1.0 + 2.0 * (a / cv) * t)) / (a / cv)
    y = np.interp(tau * RATE, np.arange(ns), weighted)
    y *= np.minimum(1.0, (n_out - np.arange(n_out)) / (0.3 * RATE))
    write("missile_launch", np.tanh(y / np.max(np.abs(y)) * 1.3), peak=0.9)


def missile_eject():
    """AKU-170 ejector: a pyro cartridge in the launcher drives two pistons that
    push the missile down and away; the motor lights only when it is clear (that
    is missile_launch, played ~0.4 s later by the game). Heard from outside:
    the cartridge's bang muffled by the launcher body, a hiss of vented gas, the
    hooks releasing and the pistons slamming into their stops - a heavy metal clunk
    - and the airframe answering the kick with a low thump."""
    n = int(0.3 * RATE)
    x = np.zeros(n)
    bang = np.zeros(n)
    add_at(bang, friedlander(0.9e-3, length=10.0), 0)
    x += filt(bang, lowpass(1400.0, 2))
    vent = burst_noise(n, log_hump(1800.0, 1.0, 1.0)) * env(n, 0.001, 0.025)
    x += 0.12 * vent / np.max(np.abs(vent))
    hooks = burst_noise(int(0.01 * RATE), log_hump(3000.0, 0.7, 0.7)) * env(int(0.01 * RATE), 0.0002, 0.0015)
    add_at(x, 0.15 * hooks / np.max(np.abs(hooks)), int(0.008 * RATE))
    clunk_at = int(0.028 * RATE)
    hit = burst_noise(int(0.015 * RATE), highpass(400.0)) * env(int(0.015 * RATE), 0.0002, 0.002)
    add_at(x, 0.35 * hit / np.max(np.abs(hit)), clunk_at)
    x += 0.55 * modes(n, [(185.0, 0.06, 1.0), (412.0, 0.045, 0.8), (737.0, 0.03, 0.55),
                          (1210.0, 0.02, 0.35), (1830.0, 0.012, 0.2)], clunk_at)
    t = np.arange(n) / RATE
    thump = np.sin(2 * np.pi * np.cumsum(90.0 * np.exp(-t / 0.08) + 50.0) / RATE) * env(n, 0.003, 0.07)
    x += 0.45 * thump
    x *= np.minimum(1.0, (n - np.arange(n)) / (0.03 * RATE))
    write("missile_eject", x, peak=0.85)


def blast_far():
    """Warhead (R-73: ~7 kg; R-77: ~22 kg) at ~3 km. The blast wave has stretched to
    a positive phase of ~10-15 ms (energy at 20-80 Hz: a thump, no crack), and 3 km
    of air remove nearly everything above ~1.5 kHz (-18 dB at 1 kHz, -45 dB at
    2 kHz). The roll that follows is the same pulse arriving again by longer paths:
    reflection from the ground/sea, then sound scattered by atmospheric turbulence -
    a smeared, irregular, decaying rumble."""
    n = int(3.0 * RATE)
    dist = 3000.0
    x = np.zeros(n)
    add_at(x, friedlander(0.013, b=1.4, length=6.0), int(0.02 * RATE))
    # Ground reflection: later, weaker, more absorbed (longer path + soft ground).
    refl = np.zeros(n)
    add_at(refl, 0.55 * friedlander(0.016, b=1.4, length=6.0), int(0.30 * RATE))
    x += filt(refl, lowpass(500.0, 2))
    # Scattering by turbulence grows with frequency, so the roll is brighter than
    # the direct thump (still under the same 3 km of absorption).
    rumble = shaped_noise(n, lambda f: log_hump(110.0, 1.0, 2.0)(f) * highpass(18.0)(f))
    rumble /= np.max(np.abs(rumble))
    roll = np.maximum(slow_mod(n, 5.0, 0.7), 0.0)  # irregular "rolling"
    t = np.arange(n) / RATE
    rumble *= roll * (1 - np.exp(-np.maximum(t - 0.05, 0) / 0.12)) * np.exp(-t / 0.75) * (t > 0.05)
    x += 0.45 * rumble
    x = filt(x, lambda f: absorbed(dist)(f) * highpass(15.0)(f))
    x *= np.minimum(1.0, (n - np.arange(n)) / (0.4 * RATE))
    write("blast_far", x, peak=0.9)


def hit():
    """30 mm HE (OFZ, ~50 g of explosive) bursting on an airframe ~400 m away: a
    short sharp crack (blast positive phase well under a millisecond, its top
    octaves trimmed by 400 m of air), fragments and the shell body striking the
    skin (a spatter of clicks over ~15 ms) and a brief, quickly damped ring of the
    struck panels. Meant to be played sparingly as hit confirmation."""
    n = int(0.25 * RATE)
    x = np.zeros(n)
    add_at(x, friedlander(0.35e-3, length=12.0), 0)
    for _ in range(12):
        cl = int(0.004 * RATE)
        c = burst_noise(cl, highpass(1500.0)) * env(cl, 0.0001, 0.0006)
        add_at(x, rng.uniform(0.08, 0.22) * c / np.max(np.abs(c)), int(rng.uniform(0.002, 0.018) * RATE))
    body = burst_noise(n, log_hump(320.0, 1.0, 1.2)) * env(n, 0.001, 0.025)
    x += 0.35 * body / np.max(np.abs(body))
    x += 0.08 * modes(n, [(690.0, 0.05, 1.0), (1130.0, 0.04, 0.8), (1720.0, 0.03, 0.6),
                          (2410.0, 0.02, 0.4)], int(0.003 * RATE))
    x = filt(x, lambda f: absorbed(400.0)(f) * highpass(60.0)(f))
    x *= np.minimum(1.0, (n - np.arange(n)) / (0.03 * RATE))
    write("hit", x, peak=0.85)


def seeker_noise(n, depth):
    """Periodic 'detector noise' envelope: an IR seeker's audio is its detector
    signal after the reticle chops it, so the tone always carries some
    scintillation - a fast random flutter (here 15-60 Hz)."""
    m = shaped_noise(n, lambda f: (f > 15.0) * (f < 60.0) * 1.0)
    m /= max(np.max(np.abs(m)), 1e-9)
    return 1.0 + depth * m


def tone_seek():
    """Seeker uncaged, no lock: a low (500 Hz), soft, growling tone that comes in
    pulses - 0.18 s on, 0.22 s off. The exact Soviet/Russian pattern is not
    documented in open sources (see sound.md); it only has to read as "searching"
    and differ clearly from tone_lock and the RI-65 attention tone."""
    n = int(0.8 * RATE)  # 2 pulse periods; 500 Hz x 0.8 s = 400 whole cycles
    t = np.arange(n) / RATE
    f0 = 500.0
    tone = np.sin(2 * np.pi * f0 * t) + 0.3 * np.sin(2 * np.pi * 2 * f0 * t + 0.7) \
        + 0.12 * np.sin(2 * np.pi * 3 * f0 * t + 1.9)
    tone *= seeker_noise(n, 0.45)  # the growl
    band = shaped_noise(n, peaks([f0], [18.0], [1.0]))  # narrow noise around the tone
    tone += 0.12 * band / np.sqrt(np.mean(band ** 2))
    period = 0.4
    ph = np.mod(t, period)
    edge = 0.015
    gate = np.clip(np.minimum(ph / edge, (0.18 - ph) / edge), 0.0, 1.0)
    gate = 0.5 - 0.5 * np.cos(np.pi * gate)  # raised-cosine edges, no clicks
    write("tone_seek", tone * gate, rms=0.05)


def tone_lock():
    """Seeker locked: a steady, continuous, higher tone (as described for the
    R-73 from cockpit videos: a steady lock tone, not a Sidewinder-style growl).
    1350 Hz with soft harmonics - higher than seek, and neither the pitch nor the
    timbre of the RI-65's 1 kHz square attention tone."""
    n = int(0.5 * RATE)  # 1350 Hz x 0.5 s = 675 whole cycles
    t = np.arange(n) / RATE
    f0 = 1350.0
    tone = np.sin(2 * np.pi * f0 * t) + 0.25 * np.sin(2 * np.pi * 2 * f0 * t + 0.4) \
        + 0.1 * np.sin(2 * np.pi * 3 * f0 * t + 1.3)
    tone *= seeker_noise(n, 0.06)  # a locked signal is strong and clean
    write("tone_lock", tone, rms=0.06)


# --- Radar warning receiver (docs/assets/sound.md, "СПО") ------------------------
# The SPO-15LM, the Su-27's RWR, puts electronic tones into the pilot's headset:
# 400 Hz while a radar in search illuminates us (intermittent: one burst per pass
# of its beam), a continuous 800 Hz tone while a radar tracks us, and 800 Hz
# interrupted every 0.25 s for the main attacking radar in track - the case its
# manual pairs with the flashing ZAKHVAT lamp, "a missile approaching". The
# Su-35S's L150 is documented only as giving "a sound alarm", so the SPO-15
# scheme is the documented one we follow. The waveform is not documented: a
# full harmonic series (a buzzer), so none of them is mistaken for the seeker's
# soft sine-like tones or the RI-65's hollow 1 kHz square.

def rwr_buzz(n, f0):
    """Steady electronic tone at f0 with every harmonic (1/k, like a pulse
    generator) up to ~4.5 kHz. The fundamental is trimmed (PSP speakers can't
    play 400 Hz; the ear still hears the pitch from the harmonics), the top is
    rolled off so it is not harsh. Schroeder phases keep the crest factor low:
    louder for a given peak. Whole cycles in n samples keep a loop seamless."""
    t = np.arange(n) / RATE
    kmax = int(4500.0 // f0)
    x = np.zeros(n)
    for k in range(1, kmax + 1):
        f = k * f0
        g = (1.0 / k) * highpass(650.0)(np.array([f]))[0] * lowpass(3200.0, 2)(np.array([f]))[0]
        x += g * np.sin(2 * np.pi * f * t - np.pi * k * (k - 1) / kmax)
    return x


def gate(t, period, on, edge=0.004):
    """1 for the first `on` seconds of every `period`, raised-cosine edges."""
    ph = np.mod(t, period)
    g = np.clip(np.minimum(ph / edge, (on - ph) / edge), 0.0, 1.0)
    return 0.5 - 0.5 * np.cos(np.pi * g)


def rwr_new():
    """New radar in search: the 400 Hz search tone. One pass of a beam gives one
    burst (the SPO holds a detection ~0.125-0.25 s); the one-shot plays two short
    bursts back to back - a game abbreviation of the first sweeps - so it reads as
    a 'beep-beep', not as one more pulse of tone_seek."""
    n = int(0.3 * RATE)
    t = np.arange(n) / RATE
    x = rwr_buzz(n, 400.0) * gate(t, 0.155, 0.105)
    write("rwr_new", x, rms=0.08)


def rwr_lock():
    """A radar tracks us: the continuous high (800 Hz) tone."""
    n = int(0.5 * RATE)  # 800 Hz x 0.5 s = 400 whole cycles
    write("rwr_lock", rwr_buzz(n, 800.0), rms=0.06)


def rwr_missile():
    """Main threat in track, missile approaching: 800 Hz interrupted every
    0.25 s (read as the repetition period: 0.125 s on, 0.125 s off - 4 beeps/s).
    Same RMS as rwr_lock over the loop, so each beep is ~3 dB louder."""
    n = int(0.5 * RATE)  # 2 beep periods, 400 whole cycles of 800 Hz
    t = np.arange(n) / RATE
    write("rwr_missile", rwr_buzz(n, 800.0) * gate(t, 0.25, 0.125), rms=0.06)


# --- Menu (docs/assets/sound.md, "Интерфейс (меню)") ------------------------------
# Cockpit switchgear heard up close, built the way it sounds in life:
#   * a part lands on its stop - a Hertz contact: a half-sine force pulse, ~40-90 us
#     steel on steel (an impulse at 22 kHz, flat to Nyquist), ~0.25-0.45 ms through a
#     textolite washer or a padded stop (modes above ~1 / contact time barely
#     excited: duller).
#     It may rebound, each bounce e times weaker and sooner (restitution);
#   * the struck parts ring as modal resonators (van den Doel & Pai): frequencies
#     from the real dimensions of a panel (plate) or a lever / rail (beam), gains
#     from the mode shape at the point struck, decay from a loss factor that barely
#     depends on frequency, tau = 1 / (pi f eta): high modes die first, the "metal"
#     cue. Screws, bushings and a hand on the control make eta 10-100x the metal's
#     own 1e-4..1e-3, hence rings of tens of ms rather than seconds;
#   * the tiny parts (contact springs, the detent ball) ring above Nyquist: a burst
#     of high-passed noise (Farnell's switch click).
# Each sound has its own seeded generator: the global `rng` stream - and every other
# sound - stays bit-identical, and `sfx.py ui` rebuilds exactly these files.

AL = (5100.0, 0.33)       # sqrt(E / rho), m/s; Poisson's ratio: aluminium alloy
STEEL = (5050.0, 0.29)
PANEL = (0.15, 0.09, 0.0015, AL)    # a x b x h, m: the console panel the switches sit in
LID = (0.20, 0.12, 0.002, STEEL)    # an ammo-box lid (ui_confirm)
BETA_L = {"free": (4.730, 7.853, 10.996, 14.137),     # beam eigenvalues: a rail
          "clamped": (1.875, 4.694, 7.855, 10.996)}   # clamped-free: a lever, a pawl
UI_SOUNDS = ("ui_move", "ui_select", "ui_back", "ui_tab", "ui_slide", "ui_deny", "ui_confirm")


def plate_modes(plate, x0, y0, eta, count=10, fmax=9500.0, pickup=(0.62, 0.71)):
    """Simply supported plate a x b x h struck at (x0 a, y0 b):
    f_mn = pi/2 * h c / sqrt(12 (1 - nu^2)) * ((m/a)^2 + (n/b)^2). Gain: the
    transfer mobility to a pickup point, the product of the mode shapes at both
    points - signed, so the modes' onsets partly cancel, as they do in a real
    panel. Only the `count` modes with the most energy (gain^2 tau) are kept: few
    of a struck object's modes matter to the ear."""
    a, b, h, (c, nu) = plate
    k = np.pi / 2 * h * c / np.sqrt(12.0 * (1.0 - nu * nu))
    out = []
    for m in range(1, 16):
        for q in range(1, 16):
            f = k * ((m / a) ** 2 + (q / b) ** 2)
            g = np.sin(m * np.pi * x0) * np.sin(q * np.pi * y0) \
                * np.sin(m * np.pi * pickup[0]) * np.sin(q * np.pi * pickup[1])
            if f < fmax and abs(g) > 0.03:
                out.append((f, 1.0 / (np.pi * f * eta), g))
    out.sort(key=lambda p: -p[2] ** 2 * p[1])
    return out[:count]


def beam_modes(length, thick, mat, ends, eta, gains, fmax=10500.0):
    """Bending modes of a flat bar `thick` deep:
    f = (beta L)^2 / (2 pi L^2) * thick / sqrt(12) * c."""
    out = []
    for bl, g in zip(BETA_L[ends], gains):
        f = bl * bl / (2 * np.pi * length ** 2) * thick / np.sqrt(12.0) * mat[0]
        if f < fmax:
            out.append((f, 1.0 / (np.pi * f * eta), g))
    return out


def scaled(partials, s):
    return [(f, d, g * s) for f, d, g in partials]


def contact(seconds):
    """Hertz contact force: a half-sine lasting the contact time, unit impulse."""
    k = max(1, int(round(seconds * RATE)))
    p = np.sin(np.pi * (np.arange(k) + 0.5) / k)
    return p / p.sum()


def impacts(n, at, amp, contact_s, gap=0.0, rebounds=0, e=0.45):
    """Force of a part landing on a stop at `at` s, then `rebounds` bounces, each
    e times weaker and after a gap e times shorter (coefficient of restitution)."""
    f = np.zeros(n)
    for _ in range(rebounds + 1):
        add_at(f, amp * contact(contact_s), int(round(at * RATE)))
        at, amp, gap = at + gap, amp * e, gap * e
    return f


def struck(force, partials):
    """Parts driven by `force`: the modal impulse response convolved with the force.
    Sound is taken as surface velocity, which an impact sets going at once: every
    mode starts at its peak (cos), so the attack is a click as sharp as the contact
    is short - the hardness cue. (Displacement-phase sines would add up to a slow
    one-sided lump below the lowest mode.)"""
    n = len(force)
    return np.convolve(force, modes(n, partials, phase=np.pi / 2))[:n]


def chatter(n, at, amp, decay, fc, r):
    """Tiny parts ringing above Nyquist: a burst of noise high-passed at fc."""
    m = int(8 * decay * RATE) + 2
    b = filt(r.standard_normal(m), highpass(fc, 2)) * env(m, 0.0001, decay)
    x = np.zeros(n)
    add_at(x, amp * b / np.max(np.abs(b)), int(round(at * RATE)))
    return x


def ui_finish(x, fade):
    """One-pole DC blocker (~14 Hz; causal, so nothing smears ahead of the
    transient), then a raised-cosine fade: the file ends on silence."""
    y = np.empty_like(x)
    px = py = 0.0
    for i, v in enumerate(x):
        py = v - px + 0.996 * py
        px = v
        y[i] = py
    k = int(fade * RATE)
    y[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(1, k + 1) / k)
    return y


def ui_move():
    """Cursor step (the most frequent sound): a ratchet detent. A spring-steel pawl
    (9 x 0.4 mm, clamped: ~4 kHz) snaps into the next tooth and bounces once; the
    panel barely answers, damped by the hand on the control. A light tick."""
    r = np.random.default_rng(0x0A01)
    n = int(0.032 * RATE)
    hit = impacts(n, 0.0, 1.0, 45e-6, gap=0.0011, rebounds=1, e=0.4)
    parts = beam_modes(0.009, 0.0004, STEEL, "clamped", 0.02, (1.0,)) \
        + scaled(plate_modes(PANEL, 0.2, 0.3, 0.05, count=6), 0.25)
    x = struck(hit, parts) + chatter(n, 0.0, 0.5, 0.0012, 3500.0, r) \
        + chatter(n, 0.0011, 0.2, 0.0008, 3500.0, r)
    write("ui_move", ui_finish(x, 0.014), peak=0.55)


def ui_select():
    """Confirm / open: a toggle switch thrown. Past dead centre the over-centre
    spring snaps the rocker - the contacts close with a bright snap and bounce;
    ~11 ms later the lever (22 x 3 mm steel, ~5 kHz) lands on the end of its slot
    and the console panel (150 x 90 x 1.5 mm aluminium, modes from 0.6 kHz) rings."""
    r = np.random.default_rng(0x0A02)
    n = int(0.115 * RATE)
    lever = beam_modes(0.022, 0.003, STEEL, "clamped", 0.008, (0.4,))
    panel = plate_modes(PANEL, 0.3, 0.4, 0.012)
    snap = impacts(n, 0.0, 0.75, 40e-6, gap=0.0009, rebounds=2, e=0.5)
    seat = impacts(n, 0.011, 1.0, 60e-6, gap=0.0014, rebounds=1, e=0.4)
    x = struck(snap, lever + scaled(panel, 0.4)) + struck(seat, lever + panel)
    x += chatter(n, 0.0, 0.4, 0.0015, 3000.0, r) + chatter(n, 0.011, 0.35, 0.0025, 2500.0, r)
    write("ui_select", ui_finish(x, 0.035), peak=0.53)


def ui_back():
    """Back / close: the same switch, a latch let go. The pawl lifts out of its
    notch (a small tick), then the return spring - weaker than a hand - lays the
    lever onto a textolite-washered stop: a 0.35 ms contact barely excites anything
    above ~3 kHz, so it is the panel's lower modes (0.6-2 kHz) that answer."""
    r = np.random.default_rng(0x0A03)
    n = int(0.105 * RATE)
    pawl = beam_modes(0.009, 0.0004, STEEL, "clamped", 0.012, (0.35,))
    panel = plate_modes(PANEL, 0.2, 0.3, 0.025)
    lift = impacts(n, 0.0, 0.45, 45e-6, gap=0.0010, rebounds=1, e=0.4)
    land = impacts(n, 0.016, 1.0, 0.35e-3, gap=0.003, rebounds=1, e=0.35)
    x = struck(lift, pawl + scaled(panel, 0.3)) + struck(land, panel)
    x += chatter(n, 0.0, 0.25, 0.001, 3000.0, r)
    write("ui_back", ui_finish(x, 0.03), peak=0.52)


def ui_tab():
    """Tab / value step: a rotary selector (wafer switch) detent. The spring ball
    drops into the next notch of the star wheel and settles against the far flank
    ~7 ms later, the rotor's wipers scrape onto the next studs, the shaft (40 x 6 mm
    steel, ~3.1 kHz) and the panel ring briefly: "chk"."""
    r = np.random.default_rng(0x0A04)
    n = int(0.07 * RATE)
    parts = beam_modes(0.04, 0.006, STEEL, "clamped", 0.006, (0.8,)) \
        + plate_modes(PANEL, 0.7, 0.55, 0.014)
    drop = impacts(n, 0.0, 1.0, 40e-6, gap=0.0008, rebounds=2, e=0.5)
    settle = impacts(n, 0.007, 0.4, 80e-6)
    x = struck(drop + settle, parts)
    x += chatter(n, 0.0, 0.3, 0.0015, 3000.0, r) + chatter(n, 0.007, 0.12, 0.001, 3000.0, r)
    m = int(0.014 * RATE)  # wipers: friction noise while the rotor still moves
    wipe = filt(r.standard_normal(m), log_hump(3800.0, 0.8, 0.8)) * env(m, 0.0005, 0.004)
    add_at(x, 0.25 * wipe / np.max(np.abs(wipe)), 0)
    write("ui_tab", ui_finish(x, 0.025), peak=0.58)


def ui_slide():
    """Campaign carousel: a carriage on a steel rail. Static friction lets go (a
    tick), then the sliding noise follows the speed - louder and brighter as it
    runs (asperities pass faster) - coloured by the rail's bending modes (200 x 6 mm
    steel bar, free ends: 0.78 / 2.1 / 4.2 / 7.0 kHz); at 0.135 s, still at a
    third of its top speed, it runs onto a padded end stop (0.25 ms contact)."""
    r = np.random.default_rng(0x0A05)
    n = int(0.2 * RATE)
    t = np.arange(n) / RATE
    stop_at = 0.135
    # speed: breaks loose in ~12 ms, slows down, meets the stop at 30 % of top speed
    v = (1.0 - np.exp(-t / 0.012)) * (1.0 - 0.7 * np.clip(t / stop_at, 0.0, 1.0) ** 1.5) * (t < stop_at)
    dark = filt(r.standard_normal(n), log_hump(1800.0, 1.0, 1.0))
    bright = filt(r.standard_normal(n), log_hump(5000.0, 0.8, 0.8))
    rub = (dark / np.std(dark) * (1.0 - v) + bright / np.std(bright) * v) * v ** 1.5
    rail = beam_modes(0.2, 0.006, STEEL, "free", 0.02, (0.6, 1.0, 0.8, 0.5))
    ringing = struck(rub, rail)
    x = 0.045 * rub + 0.045 * ringing / np.std(ringing)
    x += struck(impacts(n, 0.0, 0.35, 45e-6, gap=0.0008, rebounds=1), rail)
    x += chatter(n, 0.0, 0.3, 0.001, 3000.0, r)
    stop = impacts(n, stop_at, 0.8, 0.25e-3, gap=0.002, rebounds=1, e=0.3)
    x += struck(stop, rail + plate_modes(PANEL, 0.6, 0.35, 0.025, count=8))
    write("ui_slide", ui_finish(x, 0.035), peak=0.5)


def ui_deny():
    """Locked item: a latch tugged against its locking pin, twice, 0.1 s apart.
    Steel meets the pin through a textolite bushing (0.45 ms contact: little above
    ~2.5 kHz) and the hand on the latch damps the panel hard (eta 0.03, its five
    strongest modes only): two dull thunks, the second weaker - a "no" without a
    ring."""
    r = np.random.default_rng(0x0A06)
    n = int(0.2 * RATE)
    panel = plate_modes(PANEL, 0.15, 0.25, 0.03, count=5)
    f = impacts(n, 0.0, 1.0, 0.45e-3, gap=0.004, rebounds=1, e=0.3) \
        + impacts(n, 0.1, 0.75, 0.45e-3, gap=0.004, rebounds=1, e=0.3)
    x = struck(f, panel) + chatter(n, 0.0, 0.06, 0.001, 1500.0, r) \
        + chatter(n, 0.1, 0.05, 0.001, 1500.0, r)
    write("ui_deny", ui_finish(x, 0.04), peak=0.6)


def ui_confirm():
    """Heavy commit (start the mission, buy): an ammo-box latch. The handle (70 x
    6 mm steel, pivoted: ~1.0 and 6.3 kHz) goes over centre with a click, the bolt
    slides ~15 ms in its channel and slams home on the lid's strike plate (90 us
    contact, two bounces): the 200 x 120 x 2 mm steel lid rings from 452 Hz (its
    1409 / 1449 Hz pair beats at 40 Hz: the shimmer of struck sheet metal); the
    handle then drops onto its rest."""
    r = np.random.default_rng(0x0A07)
    n = int(0.27 * RATE)
    handle = beam_modes(0.07, 0.006, STEEL, "clamped", 0.006, (0.5, 0.1))
    lid = plate_modes(LID, 0.45, 0.4, 0.008, count=10)
    x = struck(impacts(n, 0.0, 0.5, 50e-6, gap=0.0009, rebounds=1), handle + scaled(lid, 0.2))
    x += chatter(n, 0.0, 0.35, 0.0012, 3000.0, r)
    m = int(0.016 * RATE)  # the bolt sliding in its channel
    slide = filt(r.standard_normal(m), log_hump(2500.0, 1.0, 1.0)) * np.sin(np.pi * np.arange(m) / m)
    add_at(x, 0.12 * slide / np.max(np.abs(slide)), int(0.007 * RATE))
    home = impacts(n, 0.024, 1.0, 90e-6, gap=0.0022, rebounds=2, e=0.4)
    x += struck(home, lid + handle) + chatter(n, 0.024, 0.2, 0.003, 2000.0, r)
    rest = impacts(n, 0.075, 0.3, 0.15e-3, gap=0.002, rebounds=1, e=0.4)
    x += struck(rest, handle + scaled(lid, 0.4))
    write("ui_confirm", ui_finish(x, 0.06), peak=0.55)


def a_weight(f):
    """IEC 61672 A-weighting, linear."""
    f2 = np.maximum(f, 1.0) ** 2
    ra = 12194.0 ** 2 * f2 ** 2 / ((f2 + 20.6 ** 2) * (f2 + 12194.0 ** 2)
                                    * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)))
    return ra * 10.0 ** (2.0 / 20.0)


def ui_preview():
    """Check the written files and write build/ui_sounds_preview.wav (each sound
    twice, then a menu session). L_A100: A-weighted energy of the loudest 100 ms
    (the ear sums a short sound over ~0.1 s), dB re full scale. Centroid of the
    power spectrum: on the magnitude spectrum the skirts of decaying modes drag
    every short sound towards mid-band."""
    snd = {}
    print(f"\n  {'':12s} {'ms':>4s} {'peak':>5s} {'RMS':>6s} {'L_A100':>7s} {'centroid':>8s} "
          f"{'onset':>6s} {'tail':>6s}")
    for name in UI_SOUNDS:
        with wave.open(os.path.join(OUT, name + ".wav"), "rb") as w:
            x = np.frombuffer(w.readframes(w.getnframes()), "<i2") / 32768.0
        snd[name] = x
        assert np.all(np.isfinite(x))
        p = np.max(np.abs(x))
        spec = np.abs(np.fft.rfft(x, 4 * len(x))) ** 2
        centroid = np.sum(freqs(4 * len(x)) * spec) / np.sum(spec)
        w = int(0.1 * RATE)
        xa = filt(np.concatenate([x, np.zeros(len(x) + w)]), a_weight)
        e = np.concatenate([[0.0], np.cumsum(xa * xa)])
        loud = 10 * np.log10(np.max(e[w:] - e[:-w]) / w)
        onset = np.argmax(np.abs(x) >= 0.1 * p) / RATE * 1000
        tail = 20 * np.log10(max(np.sqrt(np.mean(x[-int(0.005 * RATE):] ** 2)), 1e-9) / p)
        print(f"  {name:12s} {len(x) / RATE * 1000:4.0f} {p:5.2f} {np.sqrt(np.mean(x * x)):6.3f} "
              f"{loud:5.1f}dB {centroid:6.0f}Hz {onset:4.1f}ms {tail:4.0f}dB")
    events, t = [], 0.0
    for name in UI_SOUNDS:
        for _ in range(2):
            events.append((t, name))
            t += len(snd[name]) / RATE + 0.4
    t += 0.6
    session = [("ui_move", 0.12)] * 4 + [("ui_move", 0.35), ("ui_select", 0.7)] \
        + [("ui_move", 0.15)] * 2 + [("ui_move", 0.4)] + [("ui_tab", 0.3)] * 2 \
        + [("ui_tab", 0.5), ("ui_slide", 0.45), ("ui_slide", 0.6), ("ui_deny", 0.6),
           ("ui_back", 0.8), ("ui_confirm", 1.0)]
    for name, gap in session:
        events.append((t, name))
        t += gap
    mix = np.zeros(int(t * RATE) + 1)
    for at, name in events:
        add_at(mix, snd[name], int(at * RATE))
    path = os.path.join(ROOT, "build", "ui_sounds_preview.wav")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(np.clip(np.round(mix * 32767), -32768, 32767).astype("<i2").tobytes())
    print(f"\n  {os.path.relpath(path, ROOT)}  {len(mix) / RATE:.1f} s")


def main(args):
    """sfx.py - every sound; sfx.py ui - only the menu sounds, plus a check table
    and build/ui_sounds_preview.wav."""
    if args not in ([], ["ui"]):
        print(main.__doc__)
        return 2
    os.makedirs(OUT, exist_ok=True)
    print(f"Writing {OUT}")
    world = () if args else (engine_roar, engine_whine, afterburner, wind, buffet,
                             ab_light, flare, explosion,
                             gun_burst_and_tail, missile_launch, missile_eject, blast_far, hit,
                             tone_seek, tone_lock, rwr_new, rwr_lock, rwr_missile)
    for make in world + (ui_move, ui_select, ui_back, ui_tab, ui_slide, ui_deny, ui_confirm):
        make()
    if args:
        ui_preview()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
