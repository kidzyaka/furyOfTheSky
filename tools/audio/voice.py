#!/usr/bin/env python3
"""Voice warning messages of the RI-65 "Rita" informer (docs/decisions/0024, 0025).

Writes assets/sfx/ri_*.wav: 22.05 kHz mono 16-bit (docs/formats/wav.md).

Source per phrase, in order of preference:
  1. a real RI-65 recording in tools/audio/voice_src/<uwupad id>.ogg (fetched by
     tools/audio/fetch_voice.py; personal use only, kept out of git), or any
     tools/audio/voice_src/<name>.<wav|aiff|ogg|mp3> you drop in yourself;
  2. for the phrases the game uses: the macOS Russian voice "Milena".

Every message is rebuilt the same way, so phrases from different rips sound
like one system:
  * the recording's own attention tone is cut and replaced by a uniform one,
    measured from the recordings: 1 kHz square wave through the ~3.4 kHz
    intercom band (fundamental + 3rd harmonic at -9.5 dB), ~0.45 s;
  * the speech is gated (downward expander) to drop the hiss between words,
    band-limited to 280-3500 Hz and loudness-matched across phrases.

Usage: python3 tools/audio/voice.py
"""

import glob
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

# name, text, UwUpad id of the real recording (None: none found), used by the
# game now (gets a synthetic fallback). Names of used phrases must match
# audio::messageFile() in src/audio/voice_informer.cpp.
PHRASES = [
    ("ri_altitude_danger", "Высота опасная.", 28126, True),
    ("ri_g_limit", "Предельная перегрузка.", 28107, True),
    ("ri_aoa_limit", "Предельный угол атаки.", 28097, True),  # cleaner of two rips (28111)
    ("ri_speed_limit", "Скорость предельная.", 28113, True),
    # Not used yet: kept for the mechanics to come (M2 combat, fuel, damage).
    ("ri_launch_authorized", "Пуск разрешён.", 28109, False),
    ("ri_missile_launch", "Пуск ракеты.", 28122, False),
    ("ri_missile_front_high", "Ракета впереди выше.", 28120, False),
    ("ri_missile_front_low", "Ракета впереди ниже.", 28121, False),
    ("ri_missile_rear_high", "Ракета сзади выше.", 28116, False),
    ("ri_missile_rear_low", "Ракета сзади ниже.", 28117, False),
    ("ri_missile_left_high", "Ракета слева выше.", 28118, False),
    ("ri_missile_left_low", "Ракета слева ниже.", 28119, False),
    ("ri_missile_right_high", "Ракета справа выше.", 28114, False),
    ("ri_missile_right_low", "Ракета справа ниже.", 28115, False),
    ("ri_fire_left", "Пожар левого двигателя.", 28110, False),
    ("ri_fire_right", "Пожар правого двигателя.", 28128, False),
    ("ri_hydraulics_fail", "Отказ гидросистемы.", 28108, False),
    ("ri_check_controls", "Проверь управление.", 28102, False),
    ("ri_check_instruments", "Проверь показания.", 28134, False),
    ("ri_systems_fail_screen", "Отказ систем, смотри экран.", 28130, False),
    ("ri_autopilot_fail", "Отказ автопилота.", 28096, False),
    ("ri_radar_fail", "Отказ РЛС.", 28127, False),
    ("ri_irst_fail", "Отказ ОЛС.", 28101, False),
    ("ri_ecm_fail", "Отказ систем РЭБ.", 28100, False),
    ("ri_rwr_fail", "Отказ СПО.", 28123, False),
    ("ri_nav_fail", "Отказ НС.", 28124, False),
    ("ri_kpp_fail", "Отказ КПП.", 28098, False),
    ("ri_increase_rpm", "Увеличь обороты.", 28125, False),
    ("ri_reduce_rpm", "Сбрось обороты.", 28131, False),
    ("ri_reduce_rpm_right", "Сбрось обороты правого двигателя.", 28132, False),
    ("ri_bingo", "Рубеж возврата.", 28099, False),
    ("ri_fuel_1500", "Топливо полторы тысячи.", 28106, False),
    ("ri_fuel_800", "Топливо восемьсот.", 28104, False),
    ("ri_fuel_500", "Топливо пятьсот.", 28103, False),
]
VOICE = "Milena"
WORDS_PER_MIN = 190

TONE_HZ = 1000.0
TONE_LEN = 0.45   # s
TONE_GAP = 0.06   # s between tone and speech
SPEECH_RMS = 0.20  # active-speech loudness (-14 dBFS)


def to_pcm(src, dst):
    """Any audio file -> 22.05 kHz mono 16-bit WAV (ffmpeg reads ogg; afconvert does not)."""
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", str(RATE),
                    "-c:a", "pcm_s16le", dst], check=True)


def read_wav(path):
    with wave.open(path, "rb") as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    return len(pcm) / RATE


def synthesize(text, dst):
    with tempfile.TemporaryDirectory() as tmp:
        aiff = os.path.join(tmp, "say.aiff")
        subprocess.run(["say", "-v", VOICE, "-r", str(WORDS_PER_MIN), "-o", aiff, text], check=True)
        to_pcm(aiff, dst)


def band(x, lo, hi):
    f = np.fft.rfftfreq(len(x), 1.0 / RATE)
    h = 1.0 / np.sqrt(1.0 + (lo / np.maximum(f, 1e-3)) ** 4) / np.sqrt(1.0 + (f / hi) ** 8)
    return np.fft.irfft(np.fft.rfft(x) * h, len(x))


def frames_db(x, hop):
    n = len(x) // hop
    return 20 * np.log10(np.sqrt(np.mean(x[:n * hop].reshape(n, hop) ** 2, axis=1)) + 1e-9)


def tone_share(x, hop):
    """Per hop: share of energy on the 1 kHz / 3 kHz lines (the attention tone)."""
    win = 4 * hop
    out = np.zeros(len(x) // hop)
    f = np.fft.rfftfreq(win, 1.0 / RATE)
    lines = (np.abs(f - TONE_HZ) < 60) | (np.abs(f - 3 * TONE_HZ) < 80)
    for i in range(len(out)):
        seg = x[i * hop: i * hop + win]
        if len(seg) < win:
            break
        p = np.abs(np.fft.rfft(seg * np.hanning(win))) ** 2
        out[i] = p[lines].sum() / max(p.sum(), 1e-12)
    return out


def isolate_speech(x):
    """Cut the leading attention tone and the silence around the phrase."""
    hop = int(0.01 * RATE)
    db = frames_db(x, hop)
    share = tone_share(x, hop)[:len(db)]
    loud = db > db.max() - 30
    tone = np.nonzero(loud & (share > 0.6))[0]
    start = 0
    if len(tone) and tone[0] * hop < 0.3 * RATE:  # a tone at the very start
        # Only the unbroken run from the start: vowels can also put most of
        # their energy near 1 and 3 kHz for a frame or two.
        end = tone[0]
        for t in tone[1:]:
            if t - end > 3:
                break
            end = t
        start = end + 1
        # skip the tone's tail and the gap after it
        while start < len(db) and share[start] > 0.3:
            start += 1
    voiced = np.nonzero(loud & (np.arange(len(db)) >= start) & (share < 0.5))[0]
    a = max(voiced[0] - 2, start)
    b = min(voiced[-1] + 6, len(db))
    return x[a * hop: b * hop]


def gate(x):
    """Downward expander: 1:3 below 32 dB under the speech peak (hiss between words)."""
    hop = int(0.005 * RATE)
    db = frames_db(x, hop)
    thr = db.max() - 32.0
    g_db = np.where(db < thr, (db - thr) * 2.0, 0.0)  # 1:3 expansion
    # smooth: fast open, slow close
    g = np.empty_like(g_db)
    cur = 0.0
    for i, v in enumerate(g_db):
        cur = v if v > cur else cur + (v - cur) * 0.15
        g[i] = cur
    gain = np.repeat(10 ** (g / 20.0), hop)
    gain = np.concatenate([gain, np.full(len(x) - len(gain), gain[-1] if len(gain) else 1.0)])
    return x * gain


def normalize(x):
    hop = int(0.02 * RATE)
    db = frames_db(x, hop)
    active = db > db.max() - 20
    rms = np.sqrt(np.mean(x[:len(db) * hop].reshape(len(db), hop)[active] ** 2))
    x = x * (SPEECH_RMS / max(rms, 1e-9))
    over = np.abs(x) > 0.85
    x[over] = np.sign(x[over]) * (0.85 + 0.1 * np.tanh((np.abs(x[over]) - 0.85) / 0.1))
    return x


def attention_tone():
    t = np.arange(int(TONE_LEN * RATE)) / RATE
    x = np.sin(2 * np.pi * TONE_HZ * t) + np.sin(2 * np.pi * 3 * TONE_HZ * t) / 3.0
    fade = int(0.006 * RATE)
    env = np.ones_like(t)
    env[:fade] = np.linspace(0, 1, fade)
    env[-fade:] = np.linspace(1, 0, fade)
    x = x * env * np.linspace(1.0, 0.8, len(t))  # the recordings sag slightly over the beep
    return x / np.sqrt(np.mean(x ** 2)) * SPEECH_RMS * 0.75


def source_for(name, uid):
    if os.path.isdir(SRC):
        for f in sorted(glob.glob(os.path.join(SRC, name + ".*"))):
            return f
        if uid is not None and os.path.exists(os.path.join(SRC, f"{uid}.ogg")):
            return os.path.join(SRC, f"{uid}.ogg")
    return None


def main():
    os.makedirs(OUT, exist_ok=True)
    print(f"Writing {OUT}")
    real = synth = skipped = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, text, uid, used in PHRASES:
            raw = os.path.join(tmp, name + ".wav")
            src = source_for(name, uid)
            if src:
                to_pcm(src, raw)
                kind = "real"
            elif used:
                synthesize(text, raw)
                kind = "synthetic"
            else:
                skipped += 1
                continue
            speech = normalize(band(gate(band(isolate_speech(read_wav(raw)), 280.0, 3500.0)), 280.0, 3500.0))
            fade = int(0.004 * RATE)
            speech[:fade] *= np.linspace(0, 1, fade)
            speech[-fade:] *= np.linspace(1, 0, fade)
            x = np.concatenate([attention_tone(), np.zeros(int(TONE_GAP * RATE)), speech])
            secs = write_wav(os.path.join(OUT, name + ".wav"), x)
            real += kind == "real"
            synth += kind == "synthetic"
            print(f"  {name + '.wav':28s} {secs:4.2f} s  {kind:9s} {'game' if used else '     '}  \"{text}\"")
    print(f"{real} real, {synth} synthetic, {skipped} without a source (run tools/audio/fetch_voice.py)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
