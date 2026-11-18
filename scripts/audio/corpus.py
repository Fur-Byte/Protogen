"""Synthetic 4kHz corpus. Generator labels are independent of detector output."""
import math
import random

RATE = 4000
SCENARIOS = (
    "quiet_speech", "constant_step", "speech_ambient_step", "sustained_voiced",
    "quiet_after_noise", "noise_to_quiet", "short_bursts", "impulses", "slow_drift",
    "ambiguous_modulated_noise",
)


def signal(name, seed, *, carrier_hz=None):
    if carrier_hz is None:
        carrier_hz = (250, 500, 1000)[seed % 3]
    rng = random.Random(seed)
    samples, labels = [], []
    seconds = 10
    for i in range(seconds*RATE):
        t = i/RATE
        noise = 6
        speech = False
        amplitude = 0
        if name == "quiet_speech":
            speech = 1 <= t < 4
            amplitude = 180
        elif name == "constant_step":
            noise = 240 if 1 <= t < 8 else 6
        elif name == "speech_ambient_step":
            speech = 1 <= t < 5
            amplitude = 230
            noise = 180 if 2.5 <= t < 8 else 6
        elif name == "sustained_voiced":
            speech = 1 <= t < 8
            amplitude = 210
        elif name == "quiet_after_noise":
            noise = 240 if t < 4 else 6
            speech = 4.5 <= t < 8
            amplitude = 145
        elif name == "noise_to_quiet":
            noise = 220 if 1 <= t < 5 else 6
        elif name == "short_bursts":
            speech = 1 <= t < 6 and (t-1)%0.6 < 0.48
            amplitude = 200
        elif name == "impulses":
            noise = 6
        elif name == "slow_drift":
            noise = 6 + 170 * min(t/8, 1)
        elif name == "ambiguous_modulated_noise":
            noise = 6 if t < 1 or t >= 8 else 130 + 100*math.sin(t*2*math.pi*3)**2
        else:
            raise ValueError(name)
        if speech:
            # Continuous voiced energy plus source-dependent syllabic modulation.
            modulation = 1 if name == "sustained_voiced" else 0.65+0.35*math.sin(t*2*math.pi*3.1 + seed)**2
            amplitude *= modulation
            carrier = math.sin(2*math.pi*carrier_hz*t)
            voiced = amplitude*carrier
        else:
            voiced = 0
        value = 510 + voiced + rng.uniform(-noise, noise) + 5*math.sin(t*0.6)
        if name == "impulses" and i in (4000, 14000, 26000):
            value += 500
        samples.append(max(0,min(1023,round(value))))
        labels.append(speech)
    return samples, labels
