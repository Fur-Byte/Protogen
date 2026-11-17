# P7 audio scoring, iteration 2

Iteration 1 candidate 43519d2 failed held-out acceptance. Seed 53
`quiet_after_noise` took 112 ms to open against the 96 ms limit. Its
unambiguous totals were 42,448 ms true-open, 3,792 ms false-open and 816 ms
missed versus 43,168 ms, 50,224 ms and 96 ms for legacy. The rejected detector
is excluded from production by the default gate. Reproduce the rejected
evaluation with `python3 scripts/check-audio.py --held-out` at that commit.

Iteration 2 retains the original acceptance thresholds and signal generator.
Seeds 11, 19, 37 and 53 are now development inputs. Fresh held-out seeds
61, 74 and 78 cover all three carrier frequencies. The new seeds must remain
unevaluated until the revised detector is committed. A failed held-out
evaluation requires retaining the legacy activity detector.

The iteration 1 window-average onset test can miss credible periodic energy
above the configured sensitivity on the existing 10-sample rolling amplitude
scale. Iteration 2 will evaluate that rolling scale as corroborating evidence
only for periodic windows. Both paths retain two-window onset confirmation.
This change needs peak and offset qualification against the same impulse,
stationary noise, sustained speech and adaptation criteria.
