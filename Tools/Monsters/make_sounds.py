#!/usr/bin/env python3
"""Every monster's sounds, made from scratch (owner, Nov 27: "The SCPs and Monsters need sounds as well").

    python3 Tools/Monsters/make_sounds.py

Writes Assets/Sounds/Monsters/<kind>/<event>_<n>.wav (mono, 24 kHz, 16-bit) and Gem/Source/Monsters/MonsterSounds.h
(how many of each the game can pick from). Events: idle, alert, attack, hurt, die, and for a few kinds a loop
(SCP-173's scrape while it moves, SCP-096's crying). Seeded: a rerun makes the same files.

Built from a handful of voices: a throat (pulse wave through vowel filters: growls, moans, screams, giggles, sobs,
humming), noise shaped for breath, hiss, wings, concrete scraping, bubbling and wet tearing, clicks and snaps; then
a room (a short tail) so they sit in the levels' halls.
"""
import hashlib
import json
import wave
from pathlib import Path

import numpy as np
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
SR = 24000
OUT = ROOT / "Assets" / "Sounds" / "Monsters"

VOWELS = {  # formant centres (Hz) and widths
    "a": [(800, 80), (1150, 90), (2900, 120)], "o": [(450, 70), (800, 80), (2830, 100)],
    "u": [(325, 50), (700, 60), (2530, 170)], "e": [(400, 60), (1700, 80), (2600, 120)],
    "i": [(290, 50), (1870, 90), (2800, 100)], "uh": [(600, 70), (1040, 80), (2250, 100)],
}


class Maker:
    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)

    def r(self, lo, hi):
        return float(self.rng.uniform(lo, hi))

    # ---- building blocks
    def t(self, seconds):
        return np.arange(int(seconds * SR)) / SR

    def env(self, n, attack, release, hold=None):
        a = max(1, min(int(attack * SR), n // 2)); r = max(1, min(int(release * SR), n - a))
        e = np.ones(n)
        e[:a] = np.linspace(0, 1, a) ** 1.5
        e[-r:] *= np.linspace(1, 0, r) ** 2
        return e

    def noise(self, n, color="white"):
        x = self.rng.standard_normal(n)
        if color == "brown":
            x = np.cumsum(x); x -= signal.lfilter([1], [1, -0.995], x) * 0.0; x = signal.lfilter([1, -1], [1, -0.999], x)
        elif color == "pink":
            b, a = [0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400]
            x = signal.lfilter(b, a, x)
        return x / (np.max(np.abs(x)) + 1e-9)

    def band(self, x, lo, hi, order=2):
        lo = max(20, lo); hi = min(SR / 2 - 100, hi)
        if lo >= hi * 0.9:
            lo = hi * 0.5
        sos = signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos")
        return signal.sosfilt(sos, x)

    def lowpass(self, x, hz, order=2):
        return signal.sosfilt(signal.butter(order, min(hz, SR / 2 - 100), fs=SR, output="sos"), x)

    def highpass(self, x, hz, order=2):
        return signal.sosfilt(signal.butter(order, hz, btype="high", fs=SR, output="sos"), x)

    def pitch_track(self, n, f0, f1, vibrato=0.0, vib_hz=5.0, jitter=0.0):
        tt = np.arange(n) / SR
        f = np.geomspace(max(f0, 1), max(f1, 1), n)
        if vibrato:
            f *= 1 + vibrato * np.sin(2 * np.pi * vib_hz * tt + self.r(0, 6))
        if jitter:
            j = self.lowpass(self.rng.standard_normal(n), 30)
            f *= 1 + jitter * j / (np.max(np.abs(j)) + 1e-9)
        return f

    def pulse(self, freq, roughness=0.0):
        """A glottal-ish pulse train following freq (array), with subharmonic roughness."""
        phase = np.cumsum(freq / SR)
        saw = 2 * (phase % 1.0) - 1
        x = saw - 0.5 * np.sign(saw) * saw ** 2
        if roughness:
            sub = np.sin(np.pi * phase)  # half rate: growl
            x = x * (1 - roughness + roughness * (0.5 + 0.5 * sub))
            x += roughness * 0.25 * self.lowpass(self.rng.standard_normal(len(x)), 3000) * (0.5 + 0.5 * np.abs(sub))
        return x

    def formants(self, x, vowel, shift=1.0):
        out = np.zeros_like(x)
        for i, (fc, bw) in enumerate(VOWELS[vowel]):
            fc *= shift
            out += self.band(x, fc - bw * 1.5, fc + bw * 1.5) * (1.0 / (i + 1))
        return out

    def vowel_glide(self, x, v0, v1, shift=1.0):
        a, b = self.formants(x, v0, shift), self.formants(x, v1, shift)
        k = np.linspace(0, 1, len(x))
        return a * (1 - k) + b * k

    def drive(self, x, amount):
        return np.tanh(x * amount) / np.tanh(amount)

    def room(self, x, size=0.6, wet=0.25):
        n = int(size * SR)
        tail = self.rng.standard_normal(n) * np.exp(-np.linspace(0, 7, n))
        tail = self.lowpass(tail, 4500)
        tail[: int(0.012 * SR)] = 0
        y = signal.fftconvolve(x, tail)
        y = y / (np.max(np.abs(y)) + 1e-9)
        dry = np.concatenate([x, np.zeros(len(y) - len(x))])
        return dry * (1 - wet) + y * wet * np.max(np.abs(x))

    def cat(self, parts, gap=(0.02, 0.12)):
        out = []
        for p in parts:
            out.append(p)
            out.append(np.zeros(int(self.r(*gap) * SR)))
        return np.concatenate(out)

    # ---- voices
    def growl(self, secs, f0, vowel="uh", rough=0.6, shift=0.8, glide=None, drive=1.8):
        n = int(secs * SR)
        f = self.pitch_track(n, f0, glide or f0 * self.r(0.85, 1.1), vibrato=0.04, vib_hz=self.r(6, 11), jitter=0.08)
        x = self.formants(self.pulse(f, rough), vowel, shift)
        x += 0.1 * self.band(self.noise(n), 200, 2500) * self.env(n, 0.05, 0.2)
        return self.drive(x / (np.max(np.abs(x)) + 1e-9), drive) * self.env(n, self.r(0.05, 0.15), self.r(0.2, 0.5))

    def scream(self, secs, f0, f1, vowel="a", shift=1.0, rough=0.3):
        n = int(secs * SR)
        f = self.pitch_track(n, f0, f1, vibrato=0.06, vib_hz=self.r(7, 12), jitter=0.12)
        x = self.vowel_glide(self.pulse(f, rough), vowel, "e" if vowel == "a" else "a", shift)
        x += 0.12 * self.band(self.noise(n), 1500, 7000)
        return self.drive(x / (np.max(np.abs(x)) + 1e-9), 2.2) * self.env(n, 0.03, secs * 0.5)

    def moan(self, secs, f0, shift=0.9):
        n = int(secs * SR)
        f = self.pitch_track(n, f0 * 1.1, f0 * 0.8, vibrato=0.03, vib_hz=4, jitter=0.05)
        x = self.vowel_glide(self.pulse(f, 0.2), "o", "u", shift)
        x += 0.15 * self.band(self.noise(n), 300, 2000)
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.3, 0.8)

    def giggle(self, count, f0, shift=1.1, spread=0.25):
        parts = []
        for i in range(count):
            n = int(self.r(0.07, 0.13) * SR)
            fi = f0 * (1 + spread * np.sin(i * 1.3)) * self.r(0.95, 1.08)
            f = self.pitch_track(n, fi, fi * 0.85, jitter=0.05)
            x = self.formants(self.pulse(f, 0.1), "e" if i % 2 else "i", shift)
            x += 0.6 * self.band(self.noise(n), 1500, 6000) * np.linspace(1, 0, n)  # the "h"
            parts.append((x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.005, 0.05))
        return self.cat(parts, (0.03, 0.08))

    def sob(self, count, f0):
        parts = []
        for i in range(count):
            n = int(self.r(0.25, 0.6) * SR)
            f = self.pitch_track(n, f0 * self.r(1.0, 1.3), f0 * self.r(0.7, 0.9), vibrato=0.08, vib_hz=self.r(6, 9), jitter=0.15)
            x = self.vowel_glide(self.pulse(f, 0.15), "uh", "u", 1.15)
            x += 0.3 * self.band(self.noise(n), 800, 5000)  # breathy
            parts.append((x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.04, 0.2))
            inhale = self.band(self.noise(int(0.25 * SR)), 1200, 6000) * self.env(int(0.25 * SR), 0.1, 0.05) * 0.4
            parts.append(inhale)
        return self.cat(parts, (0.05, 0.25))

    def hum(self, notes, f0):
        parts = []
        for step in notes:
            n = int(self.r(0.3, 0.55) * SR)
            fi = f0 * 2 ** (step / 12)
            f = self.pitch_track(n, fi, fi, vibrato=0.015, vib_hz=5)
            x = self.formants(self.pulse(f, 0.0), "u", 1.0)
            parts.append((x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.06, 0.1))
        return self.cat(parts, (0.0, 0.03))

    def breath(self, secs, lo=400, hi=3500, wet=False):
        n = int(secs * SR)
        x = self.band(self.noise(n, "pink"), lo, hi)
        if wet:
            x *= 1 + 0.5 * self.lowpass(self.rng.standard_normal(n), 40)
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, secs * 0.4, secs * 0.5)

    def hiss(self, secs):
        n = int(secs * SR)
        x = self.highpass(self.noise(n), 3500)
        return x * self.env(n, 0.05, secs * 0.5) * 0.8

    def clicks(self, secs, rate, hz=3000):
        n = int(secs * SR)
        x = np.zeros(n)
        tt = 0.0
        while tt < secs - 0.01:
            i = int(tt * SR)
            m = int(0.006 * SR)
            k = self.band(self.rng.standard_normal(m), hz * 0.6, hz * 1.6) * np.exp(-np.linspace(0, 6, m))
            x[i:i + m] += k[: n - i] * self.r(0.4, 1.0)
            tt += self.r(0.5, 1.5) / rate
        return x / (np.max(np.abs(x)) + 1e-9)

    def wings(self, secs, rate=32):
        n = int(secs * SR)
        tt = np.arange(n) / SR
        am = (0.5 + 0.5 * np.sin(2 * np.pi * rate * tt * (1 + 0.05 * np.sin(2 * np.pi * 0.7 * tt)))) ** 3
        x = self.band(self.noise(n), 150, 2500) * am
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.1, 0.3)

    def scrape(self, secs):
        """Stone dragged on a floor: rough low noise, resonances, grit."""
        n = int(secs * SR)
        x = self.band(self.noise(n, "brown"), 60, 900) * 0.8
        x += self.band(self.noise(n), 900, 4000) * 0.25
        grit = (self.rng.random(n) > 0.9985) * self.rng.standard_normal(n) * 3
        x += self.band(grit, 1500, 6000)
        wobble = 0.7 + 0.3 * self.lowpass(self.rng.standard_normal(n), 6) * 8
        return (x / (np.max(np.abs(x)) + 1e-9)) * np.clip(wobble, 0.2, 1.2)

    def snap(self):
        n = int(0.35 * SR)
        crack = self.highpass(self.rng.standard_normal(n), 1500) * np.exp(-np.linspace(0, 40, n))
        crunch = self.band(self.noise(n), 200, 2500) * np.exp(-np.linspace(0, 12, n))
        thud = np.sin(2 * np.pi * 90 * np.arange(n) / SR) * np.exp(-np.linspace(0, 18, n))
        return self.drive(crack * 1.0 + crunch * 0.6 + thud * 0.7, 2.0)

    def bubbles(self, secs, rate=14, lo=200, hi=900):
        n = int(secs * SR)
        x = np.zeros(n)
        tt = 0.0
        while tt < secs - 0.06:
            m = int(self.r(0.02, 0.06) * SR)
            f = self.r(lo, hi)
            k = np.sin(2 * np.pi * np.cumsum(np.geomspace(f, f * self.r(1.4, 2.2), m)) / SR) * np.exp(-np.linspace(0, 6, m))
            i = int(tt * SR)
            x[i:i + m] += k[: n - i] * self.r(0.3, 1.0)
            tt += self.r(0.3, 1.7) / rate
        x += 0.3 * self.band(self.noise(n, "brown"), 40, 300)
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.1, 0.3)

    def tear(self, secs=0.5):
        n = int(secs * SR)
        x = self.band(self.noise(n), 300, 3000) * (self.rng.random(n) > 0.6)
        x = self.lowpass(x, 2500) * self.env(n, 0.01, secs * 0.6)
        return x / (np.max(np.abs(x)) + 1e-9)

    def thud(self, hz=70):
        n = int(0.4 * SR)
        x = np.sin(2 * np.pi * hz * np.arange(n) / SR) * np.exp(-np.linspace(0, 10, n))
        x += 0.3 * self.band(self.noise(n), 100, 800) * np.exp(-np.linspace(0, 20, n))
        return x


    # ---- more voices (Nov 28: "they all can't have the same sound"): textures that make each kind its own
    def ringmod(self, x, hz):
        return x * np.sin(2 * np.pi * hz * np.arange(len(x)) / SR)

    def comb(self, x, hz, fb=0.7):
        """A metallic ring: a short feedback delay tuned to hz."""
        d = max(1, int(SR / hz))
        y = np.copy(x)
        for i in range(d, len(y)):
            y[i] += fb * y[i - d]
        return y / (np.max(np.abs(y)) + 1e-9)

    def crush(self, x, bits=5, hold=3):
        q = 2 ** bits
        y = np.round(x * q) / q
        return np.repeat(y[::hold], hold)[: len(x)]

    def muffle(self, x, hz=900):
        return self.lowpass(x, hz, 4) * 1.6

    def reverse(self, x):
        return x[::-1].copy()

    def crackle(self, secs, rate=60):
        """Fire: pops and a rushing bed."""
        n = int(secs * SR)
        pops = (self.rng.random(n) > 1 - rate / SR) * self.rng.standard_normal(n) * 4
        x = self.band(pops, 800, 7000) + 0.25 * self.band(self.noise(n, "pink"), 200, 2500)
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.05, 0.3)

    def rustle(self, secs, rate=9):
        """Straw and cloth: bursts of bright noise."""
        n = int(secs * SR)
        am = np.clip(self.lowpass(self.rng.standard_normal(n), rate) * 6, 0, 1)
        x = self.band(self.noise(n), 2000, 9000) * am
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.05, 0.2)

    def caw(self, count=2, f0=700):
        parts = []
        for _ in range(count):
            n = int(self.r(0.22, 0.32) * SR)
            f = self.pitch_track(n, f0 * self.r(1.0, 1.2), f0 * 0.8, jitter=0.2)
            x = self.drive(self.formants(self.pulse(f, 0.8), "a", 1.6), 4)
            parts.append(x * self.env(n, 0.01, 0.12))
        return self.cat(parts, (0.08, 0.2))

    def whistle(self, secs, f0, f1):
        """Wind through a gap: a breathy sliding tone."""
        n = int(secs * SR)
        f = self.pitch_track(n, f0, f1, vibrato=0.02, vib_hz=3, jitter=0.03)
        tone = np.sin(2 * np.pi * np.cumsum(f) / SR)
        x = tone * 0.5 + self.band(self.noise(n), f0 * 0.8, f1 * 1.4) * 0.8
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, secs * 0.3, secs * 0.4)

    def gurgle(self, secs, f0=70):
        """A throat full of fluid: a growl chopped by bubbles."""
        n = int(secs * SR)
        g = self.growl(secs, f0, "o", 0.9, 0.6, drive=3)
        am = 0.4 + 0.6 * (np.sin(2 * np.pi * self.r(9, 15) * np.arange(n) / SR) > 0)
        return self.mix((g * self.lowpass(am, 60), 1.0), (self.bubbles(secs, 30, 150, 600), 0.6))

    def wheeze(self, secs):
        n = int(secs * SR)
        x = self.band(self.noise(n), 1500, 5000) * (1 + 0.8 * np.sin(2 * np.pi * 2.2 * np.arange(n) / SR))
        x += 0.4 * self.formants(self.noise(n), "i", 1.3)
        return (x / (np.max(np.abs(x)) + 1e-9)) * self.env(n, 0.15, 0.3)

    def cough(self, count=2):
        parts = []
        for _ in range(count):
            n = int(self.r(0.15, 0.25) * SR)
            x = self.drive(self.formants(self.noise(n), "uh", 0.9) + 0.3 * self.pulse(self.pitch_track(n, 140, 90), 0.8), 3)
            parts.append(x * self.env(n, 0.005, 0.12))
        return self.cat(parts, (0.1, 0.25))

    def clang(self, hz=420):
        n = int(1.2 * SR)
        tt = np.arange(n) / SR
        x = sum(np.sin(2 * np.pi * hz * r * tt) * np.exp(-tt * d) for r, d in ((1, 3), (2.76, 5), (5.4, 8), (8.9, 12)))
        x += self.highpass(self.rng.standard_normal(n), 2000) * np.exp(-tt * 60)
        return x / (np.max(np.abs(x)) + 1e-9)

    def shriek(self, secs, f0, f1):
        """Thin and piercing: high, few harmonics, fast wobble."""
        n = int(secs * SR)
        f = self.pitch_track(n, f0, f1, vibrato=0.1, vib_hz=self.r(14, 20), jitter=0.05)
        x = np.sin(2 * np.pi * np.cumsum(f) / SR) + 0.4 * np.sin(4 * np.pi * np.cumsum(f) / SR)
        x += 0.2 * self.band(self.noise(n), f0, 9000)
        return self.drive(x / (np.max(np.abs(x)) + 1e-9), 1.6) * self.env(n, 0.02, secs * 0.4)

    def whisper(self, secs, vowels=("i", "a", "u", "e")):
        n = int(secs * SR)
        seg = n // len(vowels)
        x = np.concatenate([self.formants(self.noise(seg), v, 1.1) * self.env(seg, 0.03, 0.05) for v in vowels])
        return x / (np.max(np.abs(x)) + 1e-9)

    def honk(self, count=2, f0=330):
        parts = []
        for _ in range(count):
            n = int(0.25 * SR)
            f = self.pitch_track(n, f0, f0 * 0.97)
            x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * 0.5
            parts.append(self.formants(x, "a", 1.4) * self.env(n, 0.01, 0.08))
        return self.cat(parts, (0.06, 0.12))

    def heels(self, count=4, hz=2600):
        parts = []
        for _ in range(count):
            m = int(0.09 * SR)
            k = self.band(self.rng.standard_normal(m), hz * 0.5, hz * 1.8) * np.exp(-np.linspace(0, 14, m))
            parts.append(k + 0.5 * self.thud(180)[:m])
        return self.cat(parts, (0.25, 0.32))

    def mix(self, *layers):
        n = max(len(l) for l, _ in layers)
        out = np.zeros(n)
        for l, g in layers:
            out[: len(l)] += l * g
        return out


# ---- the kinds: event -> list of makers (each a function of a Maker); every event gets 2-3 takes


def human_like(f0, shift, laugh=False, cry=False, words=None):
    return {
        "idle": [lambda m: m.moan(m.r(1.2, 2.0), f0, shift), lambda m: m.breath(m.r(1.0, 1.6), wet=True)],
        "alert": [lambda m: m.scream(m.r(0.6, 1.0), f0 * 2.2, f0 * 1.6, "a", shift), lambda m: m.growl(m.r(0.8, 1.2), f0, "a", 0.4, shift)],
        "attack": [lambda m: m.scream(m.r(0.4, 0.6), f0 * 2.6, f0 * 1.8, "a", shift, 0.5), lambda m: m.growl(0.5, f0 * 1.3, "a", 0.7, shift)],
        "hurt": [lambda m: m.scream(0.35, f0 * 2.0, f0 * 1.2, "uh", shift), lambda m: m.growl(0.4, f0 * 1.5, "uh", 0.3, shift)],
        "die": [lambda m: m.cat([m.scream(0.8, f0 * 2.0, f0 * 0.9, "a", shift), m.moan(1.2, f0 * 0.8, shift)])],
    }


def beast(f0, shift, quad=False):
    return {
        "idle": [lambda m: m.mix((m.growl(m.r(1.0, 1.6), f0, "uh", 0.7, shift), 1.0), (m.breath(1.4, 200, 1500), 0.3)),
                 lambda m: m.breath(m.r(0.8, 1.2), 300, 2500, wet=True)],
        "alert": [lambda m: m.growl(m.r(1.0, 1.4), f0 * 0.9, "a", 0.9, shift, glide=f0 * 1.4, drive=4),
                  lambda m: m.mix((m.growl(0.9, f0, "o", 0.8, shift), 1.0), (m.hiss(0.9), 0.3))],
        "attack": [lambda m: m.growl(0.5, f0 * 1.5, "a", 1.0, shift, glide=f0 * 2.0, drive=5),
                   lambda m: m.mix((m.growl(0.45, f0 * 1.3, "a", 0.9, shift), 1.0), (m.tear(0.4), 0.4))],
        "hurt": [lambda m: m.scream(0.35, f0 * 3.0, f0 * 2.0, "e", shift, 0.6), lambda m: m.growl(0.3, f0 * 2.0, "e", 0.6, shift)],
        "die": [lambda m: m.cat([m.scream(0.6, f0 * 2.5, f0, "a", shift, 0.7), m.growl(1.0, f0 * 0.7, "u", 0.8, shift)])],
    }


# Every kind has its own voice in every event (owner, Nov 28: "they all can't have the same sound"): its own pitch,
# vowel and texture (wet, burnt, metal, straw, ice, bubbles in a helmet...), so no two sound alike in a fight.
L = lambda f: f  # (reads better below)
KINDS = {
    # tall grinning thing in the dark: giggles that drop into a sub growl, a wide-mouth hiss
    "Smiler": {"idle": [L(lambda m: m.giggle(int(m.r(5, 9)), 210, 1.2)), L(lambda m: m.breath(1.4, 300, 2000, wet=True))],
               "alert": [L(lambda m: m.mix((m.giggle(7, 260, 1.3), 1.0), (m.growl(1.2, 50, "a", 0.9, 0.55, drive=4), 0.6)))],
               "attack": [L(lambda m: m.mix((m.growl(0.6, 45, "a", 1.0, 0.5, glide=90, drive=6), 1.0), (m.giggle(3, 320, 1.4), 0.5))),
                          L(lambda m: m.mix((m.hiss(0.5), 0.8), (m.growl(0.5, 55, "a", 1.0, 0.5, drive=5), 1.0)))],
               "hurt": [L(lambda m: m.giggle(3, 400, 1.5, 0.4))], "die": [L(lambda m: m.cat([m.giggle(4, 300, 1.3), m.growl(1.4, 40, "u", 0.9, 0.5)]))]},
    # a face where a face shouldn't be: stretching skin squeaks, a choked man's voice through a closed mouth
    "Faceling": {"idle": [L(lambda m: m.muffle(m.moan(1.6, 115, 1.0), 700)), L(lambda m: m.mix((m.clicks(1.2, 5, 4000), 0.5), (m.breath(1.2, 600, 2500), 0.6)))],
                 "alert": [L(lambda m: m.muffle(m.scream(0.9, 260, 200, "uh", 1.0, 0.3), 1100))],
                 "attack": [L(lambda m: m.mix((m.muffle(m.scream(0.5, 300, 220, "uh", 1.0, 0.6), 1200), 1.0), (m.tear(0.4), 0.7)))],
                 "hurt": [L(lambda m: m.muffle(m.growl(0.35, 180, "uh", 0.4, 1.0), 900))], "die": [L(lambda m: m.muffle(m.moan(1.8, 90, 0.9), 600))]},
    "Deathmoth": {"idle": [L(lambda m: m.wings(m.r(1.2, 2.0), 34)), L(lambda m: m.mix((m.wings(1.4, 28), 1.0), (m.clicks(1.4, 8, 2500), 0.4)))],
                  "alert": [L(lambda m: m.mix((m.wings(1.0, 44), 1.0), (m.shriek(0.6, 2400, 3200), 0.35)))],
                  "attack": [L(lambda m: m.mix((m.wings(0.6, 55), 1.0), (m.hiss(0.6), 0.5), (m.clicks(0.6, 30, 3500), 0.5)))],
                  "hurt": [L(lambda m: m.shriek(0.25, 3000, 2000))], "die": [L(lambda m: m.mix((m.wings(1.0, 16), 0.8), (m.clicks(1.0, 20, 2000), 0.4)))]},
    "Scp096": {"idle": [L(lambda m: m.sob(int(m.r(3, 5)), 230)), L(lambda m: m.sob(int(m.r(2, 4)), 260))],
               "loop": [L(lambda m: m.sob(7, 240))],
               "alert": [L(lambda m: m.scream(2.4, 380, 900, "a", 1.15, 0.45)), L(lambda m: m.scream(2.0, 450, 1100, "a", 1.2, 0.5))],
               "attack": [L(lambda m: m.scream(1.2, 700, 1200, "a", 1.2, 0.6))], "hurt": [L(lambda m: m.scream(0.6, 600, 900, "e", 1.2))],
               "die": [L(lambda m: m.scream(1.0, 600, 300, "a", 1.1))]},
    "Scp173": {"loop": [L(lambda m: m.scrape(2.5))], "attack": [L(lambda m: m.snap()), L(lambda m: m.mix((m.snap(), 1.0), (m.thud(60), 0.5)))],
               "idle": [L(lambda m: m.scrape(0.6) * m.env(int(0.6 * SR), 0.05, 0.3))], "alert": [L(lambda m: m.scrape(0.4))],
               "hurt": [L(lambda m: m.thud(140))], "die": [L(lambda m: m.thud(50))]},
    # the jumpsuit one, all skin and bone: a dry rattling wheeze, choking
    "Wretch": {"idle": [L(lambda m: m.wheeze(m.r(1.2, 1.8))), L(lambda m: m.cat([m.cough(2), m.wheeze(0.8)]))],
               "alert": [L(lambda m: m.mix((m.growl(1.0, 140, "i", 0.95, 1.1, glide=220, drive=5), 1.0), (m.wheeze(1.0), 0.6)))],
               "attack": [L(lambda m: m.mix((m.growl(0.5, 200, "i", 1.0, 1.15, glide=260, drive=6), 1.0), (m.rustle(0.5, 20), 0.3))),
                          L(lambda m: m.cat([m.cough(1), m.growl(0.4, 220, "e", 1.0, 1.1, drive=6)]))],
               "hurt": [L(lambda m: m.cough(2))], "die": [L(lambda m: m.cat([m.growl(0.6, 160, "i", 0.9, 1.1), m.wheeze(1.6)]))]},
    "Hound": {"idle": [L(lambda m: m.breath(m.r(0.8, 1.1), 400, 3000)), L(lambda m: m.growl(1.2, 80, "o", 0.8, 0.9))],
              "alert": [L(lambda m: m.cat([m.growl(0.25, 260, "a", 0.6, 1.0, drive=4), m.growl(0.25, 250, "a", 0.6, 1.0, drive=4), m.growl(0.25, 270, "a", 0.6, 1.0, drive=4)], (0.08, 0.14)))],
              "attack": [L(lambda m: m.mix((m.growl(0.5, 130, "a", 1.0, 0.9, glide=200, drive=6), 1.0), (m.snap(), 0.4)))],
              "hurt": [L(lambda m: m.shriek(0.3, 900, 600))], "die": [L(lambda m: m.cat([m.shriek(0.5, 800, 400), m.breath(1.0, 300, 2000)]))]},
    # wears people: wet flesh, a mimicked man's voice that comes out backwards
    "Skinstealer": {"idle": [L(lambda m: m.cat([m.breath(0.5, wet=True), m.reverse(m.whisper(0.9))])), L(lambda m: m.mix((m.tear(1.0), 0.4), (m.breath(1.0, 300, 2500, wet=True), 1.0)))],
                    "alert": [L(lambda m: m.mix((m.reverse(m.scream(0.9, 170, 120, "a", 0.95, 0.4)), 1.0), (m.tear(0.9), 0.5)))],
                    "attack": [L(lambda m: m.mix((m.ringmod(m.scream(0.6, 240, 160, "a", 0.95, 0.7), 60) , 1.0), (m.tear(0.6), 0.8))),
                               L(lambda m: m.mix((m.reverse(m.growl(0.6, 110, "e", 0.8, 1.0, drive=5)), 1.0), (m.snap(), 0.5)))],
                    "hurt": [L(lambda m: m.mix((m.tear(0.4), 1.0), (m.growl(0.3, 150, "e", 0.6, 1.0), 0.5)))],
                    "die": [L(lambda m: m.cat([m.reverse(m.scream(0.8, 220, 120, "a", 0.95)), m.tear(1.0)]))]},
    "Clump": {"idle": [L(lambda m: m.bubbles(m.r(1.4, 2.0), 10, 150, 600)), L(lambda m: m.mix((m.bubbles(1.5, 16), 1.0), (m.moan(1.5, 70, 0.7), 0.4)))],
              "alert": [L(lambda m: m.mix((m.growl(1.2, 55, "o", 1.0, 0.6), 1.0), (m.bubbles(1.2, 25), 0.6)))],
              "attack": [L(lambda m: m.mix((m.tear(0.6), 1.0), (m.bubbles(0.6, 30, 300, 1200), 0.7)))],
              "hurt": [L(lambda m: m.bubbles(0.5, 40, 400, 1500))], "die": [L(lambda m: m.mix((m.bubbles(1.6, 30), 1.0), (m.moan(1.6, 60, 0.6), 0.7)))]},
    # eyeless and full of teeth, mimics: clicks, a deep roar
    "Scp939": {"idle": [L(lambda m: m.mix((m.clicks(1.2, 12, 2200), 0.6), (m.breath(1.2, 200, 1500, wet=True), 0.8))), L(lambda m: m.growl(1.4, 55, "u", 0.9, 0.65))],
               "alert": [L(lambda m: m.mix((m.growl(1.2, 50, "a", 1.0, 0.6, glide=80, drive=5), 1.0), (m.clicks(1.2, 25, 2600), 0.5)))],
               "attack": [L(lambda m: m.mix((m.growl(0.6, 60, "a", 1.0, 0.6, glide=110, drive=7), 1.0), (m.snap(), 0.6)))],
               "hurt": [L(lambda m: m.growl(0.4, 120, "e", 0.9, 0.7, drive=4))], "die": [L(lambda m: m.cat([m.growl(0.8, 70, "a", 1.0, 0.6), m.clicks(1.2, 6, 1800)]))]},
    "Scp106": {"idle": [L(lambda m: m.mix((m.bubbles(m.r(1.5, 2.2), 12, 120, 500), 1.0), (m.breath(2.0, 150, 1200, wet=True), 0.6))), L(lambda m: m.giggle(4, 95, 0.75, 0.15))],
               "alert": [L(lambda m: m.mix((m.giggle(6, 90, 0.7, 0.2), 1.0), (m.bubbles(1.4, 14), 0.5)))],
               "attack": [L(lambda m: m.mix((m.growl(0.8, 60, "o", 0.7, 0.6), 1.0), (m.tear(0.6), 0.5), (m.bubbles(0.8, 25), 0.5)))],
               "hurt": [L(lambda m: m.bubbles(0.8, 35, 150, 700))], "die": [L(lambda m: m.bubbles(2.0, 25, 100, 400))]},
    # store staff: a flat tannoy-like voice (crushed, band-limited), "hmm" humming
    "Staff3008": {"idle": [L(lambda m: m.crush(m.band(m.moan(1.6, 105, 1.0), 300, 3000), 5, 2)), L(lambda m: m.hum([0, 0, -2, -2, -4], 120))],
                  "alert": [L(lambda m: m.crush(m.band(m.growl(1.0, 110, "e", 0.3, 1.0, drive=2), 300, 3200), 4, 3))],
                  "attack": [L(lambda m: m.crush(m.band(m.scream(0.5, 260, 180, "e", 1.0, 0.4), 300, 3200), 4, 3))],
                  "hurt": [L(lambda m: m.crush(m.band(m.growl(0.3, 150, "uh", 0.3, 1.0), 300, 3000), 4, 2))],
                  "die": [L(lambda m: m.crush(m.band(m.moan(1.4, 90, 1.0), 300, 3000), 3, 4))]},
    "Partygoer": {"idle": [L(lambda m: m.giggle(int(m.r(5, 8)), 280, 1.25)), L(lambda m: m.giggle(6, 240, 1.2, 0.35))],
                  "alert": [L(lambda m: m.giggle(9, 340, 1.35, 0.5))],
                  "attack": [L(lambda m: m.mix((m.shriek(0.5, 1200, 1600), 0.8), (m.giggle(3, 380, 1.4), 1.0)))],
                  "hurt": [L(lambda m: m.giggle(2, 450, 1.5))], "die": [L(lambda m: m.cat([m.giggle(3, 300, 1.3), m.giggle(3, 200, 1.1, 0.1)]))]},
    "Dancer": {"idle": [L(lambda m: m.hum([0, 4, 7, 4, 0, -5], 200)), L(lambda m: m.heels(4, 2400))],
               "alert": [L(lambda m: m.cat([m.heels(3, 2600), m.hum([7, 7, 12], 210)]))],
               "attack": [L(lambda m: m.mix((m.scream(0.5, 520, 700, "i", 1.25, 0.2), 1.0), (m.heels(1, 3000), 0.6)))],
               "hurt": [L(lambda m: m.scream(0.3, 600, 450, "i", 1.25, 0.2))], "die": [L(lambda m: m.hum([0, -1, -3, -5, -8], 200))]},
    # the plague doctor: a calm low voice, a doctor's hum, a cane tap
    "Scp049": {"idle": [L(lambda m: m.hum([0, -1, 0, 3, 0], 105)), L(lambda m: m.breath(1.5, 300, 2500))],
               "alert": [L(lambda m: m.cat([m.thud(260), m.hum([0, 0, 5], 98)]))],
               "attack": [L(lambda m: m.mix((m.growl(0.6, 90, "o", 0.2, 0.9, drive=1.4), 1.0), (m.thud(220), 0.5)))],
               "hurt": [L(lambda m: m.growl(0.3, 110, "uh", 0.2, 0.9, drive=1.3))], "die": [L(lambda m: m.moan(1.6, 85, 0.9))]},
    "Scarecrow": {"idle": [L(lambda m: m.rustle(m.r(1.2, 1.8))), L(lambda m: m.caw(2, 650))],
                  "alert": [L(lambda m: m.mix((m.caw(3, 750), 1.0), (m.rustle(1.0, 14), 0.6)))],
                  "attack": [L(lambda m: m.mix((m.rustle(0.5, 30), 1.0), (m.growl(0.5, 90, "a", 0.9, 0.8, drive=3), 0.5), (m.caw(1, 900), 0.6)))],
                  "hurt": [L(lambda m: m.rustle(0.4, 30))], "die": [L(lambda m: m.cat([m.rustle(0.8, 20), m.caw(1, 500)]))]},
    # inside a brass helmet: everything muffled, bubbling, a ring off the glass
    "Diver": {"idle": [L(lambda m: m.bubbles(m.r(1.6, 2.4), 9, 180, 700)), L(lambda m: m.muffle(m.breath(1.8, 200, 1400, wet=True), 700))],
              "alert": [L(lambda m: m.mix((m.comb(m.muffle(m.growl(1.0, 80, "o", 0.6, 0.8), 1200), 180, 0.6), 1.0), (m.bubbles(1.0, 30), 0.6)))],
              "attack": [L(lambda m: m.mix((m.comb(m.muffle(m.scream(0.6, 200, 150, "o", 0.8, 0.5), 1400), 180, 0.6), 1.0), (m.clang(300), 0.4)))],
              "hurt": [L(lambda m: m.clang(340))], "die": [L(lambda m: m.mix((m.bubbles(2.0, 40, 150, 900), 1.0), (m.muffle(m.moan(1.6, 70, 0.8), 600), 0.6)))]},
    # burnt alive: crackling, a hoarse scorched rasp
    "Burnt": {"idle": [L(lambda m: m.crackle(m.r(1.4, 2.0), 40)), L(lambda m: m.mix((m.crackle(1.6, 25), 0.7), (m.wheeze(1.6), 0.6)))],
              "alert": [L(lambda m: m.mix((m.growl(1.0, 95, "a", 1.0, 0.85, drive=7), 1.0), (m.crackle(1.0, 80), 0.6)))],
              "attack": [L(lambda m: m.mix((m.scream(0.5, 230, 160, "a", 0.85, 0.9), 1.0), (m.crackle(0.5, 140), 0.7)))],
              "hurt": [L(lambda m: m.crackle(0.4, 160))], "die": [L(lambda m: m.cat([m.growl(0.6, 90, "a", 1.0, 0.85, drive=6), m.crackle(1.6, 50)]))]},
    # hospital gown, out of its mind: fast panting, hysterical laugh-sobbing
    "Patient": {"idle": [L(lambda m: m.cat([m.breath(0.25, 600, 3000)] * int(m.r(5, 8)), (0.05, 0.1))), L(lambda m: m.sob(3, 200))],
                "alert": [L(lambda m: m.mix((m.giggle(6, 260, 1.15, 0.6), 1.0), (m.sob(2, 240), 0.6)))],
                "attack": [L(lambda m: m.scream(0.7, 380, 520, "e", 1.1, 0.3))],
                "hurt": [L(lambda m: m.sob(1, 260))], "die": [L(lambda m: m.cat([m.scream(0.6, 400, 250, "e", 1.1), m.sob(2, 180)]))]},
    "Nurse": {"idle": [L(lambda m: m.hum([0, 2, 4, 2, 0, 0], 220)), L(lambda m: m.heels(5, 2000))],
              "alert": [L(lambda m: m.shriek(0.8, 1100, 1500))],
              "attack": [L(lambda m: m.mix((m.shriek(0.6, 1400, 1900), 1.0), (m.heels(1, 2200), 0.5)))],
              "hurt": [L(lambda m: m.shriek(0.3, 1600, 1100))], "die": [L(lambda m: m.cat([m.shriek(0.7, 1300, 600), m.hum([0, -2, -4], 200)]))]},
    "Clown": {"idle": [L(lambda m: m.giggle(int(m.r(7, 11)), 230, 1.2, 0.4)), L(lambda m: m.honk(2, 320))],
              "alert": [L(lambda m: m.cat([m.honk(2, 360), m.giggle(8, 300, 1.3, 0.5)]))],
              "attack": [L(lambda m: m.mix((m.growl(0.5, 150, "a", 0.8, 1.1, drive=5), 1.0), (m.honk(1, 250), 0.7)))],
              "hurt": [L(lambda m: m.honk(1, 420))], "die": [L(lambda m: m.cat([m.giggle(5, 260, 1.2, 0.2), m.honk(1, 180)]))]},
    # lost in the tunnels: coughing, a pick on rock
    "Miner": {"idle": [L(lambda m: m.cough(int(m.r(2, 4)))), L(lambda m: m.mix((m.clang(520), 0.5), (m.breath(1.2, 300, 2000), 0.7)))],
              "alert": [L(lambda m: m.cat([m.clang(480), m.growl(0.8, 85, "o", 0.6, 0.85, drive=3)]))],
              "attack": [L(lambda m: m.mix((m.growl(0.5, 100, "a", 0.8, 0.85, drive=4), 1.0), (m.clang(440), 0.7)))],
              "hurt": [L(lambda m: m.cough(1))], "die": [L(lambda m: m.cat([m.cough(3), m.moan(1.2, 70, 0.8)]))]},
    # cold: a whistling wind, an icy breath, glass-thin cries
    "Frostwraith": {"idle": [L(lambda m: m.whistle(m.r(1.6, 2.4), 700, 500)), L(lambda m: m.breath(2.0, 2500, 9000))],
                    "alert": [L(lambda m: m.mix((m.whistle(1.2, 900, 1600), 1.0), (m.shriek(1.0, 1800, 2400), 0.4)))],
                    "attack": [L(lambda m: m.mix((m.comb(m.whistle(0.6, 1500, 800), 1200, 0.5), 1.0), (m.hiss(0.6), 0.6)))],
                    "hurt": [L(lambda m: m.comb(m.shriek(0.3, 2000, 1500), 1500, 0.5))], "die": [L(lambda m: m.whistle(2.0, 800, 200))]},
    # a farm animal gone wrong: bellows
    "Farmthing": {"idle": [L(lambda m: m.moan(m.r(1.4, 2.0), 75, 0.6)), L(lambda m: m.breath(1.2, 150, 900, wet=True))],
                  "alert": [L(lambda m: m.growl(1.6, 70, "o", 0.6, 0.55, glide=110, drive=3))],
                  "attack": [L(lambda m: m.mix((m.growl(0.7, 90, "o", 1.0, 0.55, glide=140, drive=6), 1.0), (m.thud(55), 0.7)))],
                  "hurt": [L(lambda m: m.growl(0.4, 120, "o", 0.8, 0.6, drive=4))], "die": [L(lambda m: m.moan(2.0, 60, 0.55))]},
    "Crawler": {"idle": [L(lambda m: m.clicks(m.r(1.0, 1.6), 18, 2600)), L(lambda m: m.mix((m.clicks(1.2, 10, 2000), 1.0), (m.hiss(1.2), 0.3)))],
                "alert": [L(lambda m: m.mix((m.clicks(1.0, 45, 3000), 1.0), (m.hiss(1.0), 0.6)))],
                "attack": [L(lambda m: m.mix((m.hiss(0.5), 1.0), (m.clicks(0.5, 60, 3500), 0.8), (m.snap(), 0.4)))],
                "hurt": [L(lambda m: m.clicks(0.3, 80, 4000))], "die": [L(lambda m: m.clicks(1.4, 12, 1800))]},
    "Doll": {"idle": [L(lambda m: m.giggle(3, 420, 1.4, 0.1))], "alert": [L(lambda m: m.giggle(5, 480, 1.5, 0.2))],
             "attack": [L(lambda m: m.mix((m.snap(), 0.6), (m.giggle(2, 500, 1.5), 1.0)))], "hurt": [L(lambda m: m.thud(300))], "die": [L(lambda m: m.thud(250))]},
    # hollow plastic: knocks and creaks with a hollow ring, joints clicking into place (SCP-173 is stone)
    "Mannequin": {"idle": [L(lambda m: m.comb(m.clicks(0.8, 5, 1400), 520, 0.75))],
                  "alert": [L(lambda m: m.comb(m.clicks(0.6, 22, 1600), 480, 0.8))],
                  "attack": [L(lambda m: m.mix((m.comb(m.snap(), 600, 0.7), 1.0), (m.clicks(0.4, 30, 1800), 0.6)))],
                  "hurt": [L(lambda m: m.comb(m.thud(320), 700, 0.7))], "die": [L(lambda m: m.comb(m.cat([m.thud(260), m.clicks(1.0, 10, 1200)]), 450, 0.75))],
                  "loop": [L(lambda m: m.comb(m.clicks(2.0, 4, 1200), 500, 0.7) * 0.6)]},
    "Plush": {"idle": [L(lambda m: m.giggle(3, 380, 1.4, 0.15))], "alert": [L(lambda m: m.giggle(4, 330, 1.3))],
              "attack": [L(lambda m: m.mix((m.tear(0.4), 1.0), (m.giggle(2, 350, 1.3), 0.6)))], "hurt": [L(lambda m: m.tear(0.3))], "die": [L(lambda m: m.tear(0.6))]},
    "Watcher": {"idle": [L(lambda m: m.mix((m.breath(2.4, 80, 600), 1.0), (m.hum([0, 0, 0], 55), 0.3)))], "alert": [L(lambda m: m.breath(1.6, 60, 400))]},
}

TAKES = 3


def write_wav(path, x):
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.89
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())


def main():
    counts = {}
    total = 0
    for kind, events in KINDS.items():
        counts[kind] = {}
        for event, makers in events.items():
            n = 0
            for take in range(TAKES if event != "loop" else 1):
                seed = int(hashlib.sha1(f"{kind}/{event}/{take}".encode()).hexdigest()[:8], 16)
                m = Maker(seed)
                x = makers[take % len(makers)](m)
                if event != "loop":
                    x = m.room(x, 0.7 if kind not in ("Scp096",) else 1.0, 0.22)
                else:  # seamless: cross-fade the end into the start
                    k = int(0.25 * SR)
                    x[:k] = x[:k] * np.linspace(0, 1, k) + x[-k:] * np.linspace(1, 0, k)
                    x = x[:-k]
                write_wav(OUT / kind.lower() / f"{event}_{n + 1}.wav", x)
                n += 1
                total += 1
            counts[kind][event] = n
    events = ["idle", "alert", "attack", "hurt", "die", "loop"]
    lines = ["// Generated by Tools/Monsters/make_sounds.py: how many takes of each monster sound there are.", "#pragma once", "",
             "namespace NoclipProtocol::MonsterSounds", "{",
             "    enum Event : int { Idle = 0, Alert, Attack, Hurt, Die, Loop, EventCount };",
             '    inline constexpr const char* EventNames[EventCount] = { "idle", "alert", "attack", "hurt", "die", "loop" };',
             "    struct Kind { const char* m_kind; const char* m_folder; int m_takes[EventCount]; };",
             f"    inline constexpr Kind Kinds[{len(counts)}] = {{"]
    for kind, c in counts.items():
        lines.append(f'        {{ "{kind}", "assets/sounds/monsters/{kind.lower()}", {{ {", ".join(str(c.get(e, 0)) for e in events)} }} }},')
    lines += ["    };", "} // namespace NoclipProtocol::MonsterSounds", ""]
    (ROOT / "Gem" / "Source" / "Monsters" / "MonsterSounds.h").write_text("\n".join(lines))
    (OUT / "README.md").write_text("# Monster sounds\n\nMade by `Tools/Monsters/make_sounds.py` (rerun to remake). One folder per kind: "
                                   "`idle`, `alert`, `attack`, `hurt`, `die` takes, and `loop` for the ones that keep a sound going "
                                   "(SCP-173's scrape while it moves, SCP-096's crying, the mannequin's drag).\n")
    print(total, "sounds for", len(counts), "kinds")


if __name__ == "__main__":
    main()
