# P7 frozen audio scoring protocol, iteration 1

This protocol is committed before detector implementation or benchmark tuning.
The reference is merged P6 at 8e741ebd2cf97939b2ca1b24ce034dff3b29e6e5.
All clips are synthetic 4 kHz 10-bit samples. Labels describe the generating
source. Neither the sample values nor labels originate from the candidate
detector. The benchmark executes the actual microphone shell and renderer
in both compile modes with the same sample stream and settings.

Development uses seeds 11 and 19. Held-out acceptance uses seeds 37 and 53
only after the algorithm is fixed. Held-out output must not determine parameter
changes within this iteration. Any such change requires a new documented
iteration with fresh acceptance seeds.

Required cases are quiet/speech/silence, constant noise step, speech across
an ambient step, sustained voiced speech, quiet speech after loud noise,
noise becoming quiet, repeated short bursts, isolated impulses, slow ambient
drift, and continuous noise with speech-like amplitude modulation. Additional
rail, drop, restart, sleep, calibration and backpressure tests exercise the
existing firmware recovery contract.

Labels are scored per completed 64-sample window, with a majority-of-samples
speech label. Report true/false/missed milliseconds, precision, recall,
onset/release delays, mouth transitions, noise estimate, modeled sample drops
and queue high-water. Report actual failures by clip as well as aggregate
results. Frame windows, rather than host wall clock, define acoustic delay.

Acceptance for unambiguous clips requires aggregate precision >=0.85 and
recall >=0.90, sustained voiced recall >=0.95, first onset <=96 ms, final
release <=256 ms, impulse false-open <=64 ms, and stationary noise-step
false-open <=3000 ms. Compared with legacy, the aggregate unambiguous
false-open duration must decrease >=40%, missed speech must increase <=5%
of total labeled speech, and normal-load modeled sample drops must stay zero.
For amplitude-modulated speech-like noise, report the ambiguity separately.
False-open may equal legacy and must not exceed it by >256 ms. No claim of
perfect source classification follows from synthetic labels.

The 64-sample window and configured sensitivity, noise margin, attack, release
and speaking hold retain their existing units and roles. Hardware cadence,
ring capacity, ISR, recovery and calibration remain authoritative. Physical
acoustic accuracy and execution tails require separately authorized hardware
captures. If held-out acceptance fails, preserve legacy activity and document
the failed experiment.
