#!/usr/bin/env python3
"""Generates the Level 0 ambience as seamless loops (16-bit mono WAV).

    python3 make_sounds.py <output folder>

level0_hum.wav       fluorescent buzz for each working ceiling light (4 s loop)
level0_roomtone.wav  faint low air/building rumble heard everywhere (8 s loop)
level0_zap.wav       short electrical crackle for flickering lights (one-shot)

Every looped part is built from whole cycles over the loop length, so the
start and end join without a click.
"""
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 44100


def periodic_noise(seconds, lo, hi, tilt=0.0, seed=0):
    """Band-limited noise that loops perfectly (random-phase spectrum)."""
    n = int(RATE * seconds)
    rng = np.random.default_rng(seed)
    freqs = np.fft.rfftfreq(n, 1.0 / RATE)
    mag = np.where((freqs >= lo) & (freqs <= hi), 1.0, 0.0)
    mag[1:] *= freqs[1:] ** (-tilt)
    spec = mag * np.exp(1j * rng.uniform(0, 2 * np.pi, freqs.size))
    out = np.fft.irfft(spec, n)
    return out / (np.abs(out).max() + 1e-9)


def write_wav(path, samples, peak=0.8):
    samples = samples / (np.abs(samples).max() + 1e-9) * peak
    data = (samples * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())
    print("wrote", path)


def hum():
    seconds = 4.0
    t = np.arange(int(RATE * seconds)) / RATE
    buzz = np.zeros_like(t)
    # 120 Hz mains buzz with odd-heavy harmonics, slightly clipped like a ballast
    for k, amp in [(1, 1.0), (2, 0.55), (3, 0.45), (4, 0.2), (5, 0.25), (7, 0.12), (9, 0.06)]:
        buzz += amp * np.sin(2 * np.pi * 120 * k * t + k * 0.7)
    buzz = np.tanh(buzz * 1.6)
    # slow wobble in loudness (whole cycles: 0.5 Hz and 1.25 Hz over 4 s)
    wobble = 1.0 + 0.08 * np.sin(2 * np.pi * 0.5 * t) + 0.05 * np.sin(2 * np.pi * 1.25 * t + 1.0)
    whine = 0.035 * np.sin(2 * np.pi * 7650 * t)          # thin high ballast whine
    hiss = 0.06 * periodic_noise(seconds, 2000, 9000, seed=1)
    return buzz * wobble * 0.6 + whine + hiss


def roomtone():
    seconds = 8.0
    t = np.arange(int(RATE * seconds)) / RATE
    rumble = periodic_noise(seconds, 25, 400, tilt=1.0, seed=2)
    air = 0.25 * periodic_noise(seconds, 400, 3000, tilt=0.5, seed=3)
    drone = 0.15 * np.sin(2 * np.pi * 60 * t) + 0.08 * np.sin(2 * np.pi * 55.25 * t)  # beating drone
    swell = 1.0 + 0.2 * np.sin(2 * np.pi * t / seconds)
    return (rumble + air + drone) * swell


def zap():
    """Fluorescent tube sputtering on: a sharp snap, then a harsh clipped buzz with crackle that dies away."""
    seconds = 0.6
    t = np.arange(int(RATE * seconds)) / RATE
    rng = np.random.default_rng(4)
    buzz = np.zeros_like(t)
    for k, amp in [(1, 1.0), (2, 0.7), (3, 0.6), (5, 0.4), (7, 0.25)]:
        buzz += amp * np.sin(2 * np.pi * 120 * k * t)
    buzz = np.tanh(buzz * 3.0)                                   # harsh, nearly square
    crackle = rng.standard_normal(t.size) * (rng.random(t.size) < 0.25)
    crackle = np.convolve(crackle, np.ones(6) / 6, mode="same") * 2.5
    snap = rng.standard_normal(t.size) * np.exp(-t * 120) * 0.9   # the initial pop
    env = np.clip(t / 0.003, 0, 1) * np.where(t < 0.18, 1.0, np.exp(-(t - 0.18) * 9))
    stutter = 0.6 + 0.4 * (np.sin(2 * np.pi * 23 * t) > -0.3)     # uneven, sputtering
    return (buzz * 0.8 + crackle * 0.6) * env * stutter + snap


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)
    write_wav(out / "level0_hum.wav", hum(), 0.7)
    write_wav(out / "level0_roomtone.wav", roomtone(), 0.6)
    write_wav(out / "level0_zap.wav", zap(), 0.95)


if __name__ == "__main__":
    main()
