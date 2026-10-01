#!/usr/bin/env python3
"""Generate polyphase FIR interpolation coefficients for audio_dsp.c.

No numpy: windowed-sinc low-pass, Kaiser window (I0 by series), per-phase
normalised so each polyphase subfilter has unity DC gain
(y[n*L+p] = sum_k h_p[k] * x[n-k]).
"""
import math
import sys

FS_OUT = 48000
TAPS_PER_PHASE = 32


def i0(x):
    """Modified Bessel function of the first kind, order 0 (series)."""
    s, term, k = 1.0, 1.0, 1
    while True:
        term *= (x * x) / (4.0 * k * k)
        s += term
        if term < 1e-12 * s:
            return s
        k += 1


def design(L, fc_hz, beta, name, fs_out=FS_OUT):
    N = L * TAPS_PER_PHASE                     # prototype length
    fc = fc_hz / (fs_out / 2.0)                # cutoff, fraction of output Nyquist
    M = N - 1
    denom = i0(beta)
    proto = []
    for n in range(N):
        x = n - M / 2.0
        if abs(x) < 1e-9:
            s = fc
        else:
            s = math.sin(math.pi * fc * x) / (math.pi * x)
        r = 2.0 * n / M - 1.0                  # -1 .. 1
        w = i0(beta * math.sqrt(max(0.0, 1.0 - r * r))) / denom
        proto.append(s * w)

    phases = []
    for p in range(L):
        taps = [proto[p + k * L] for k in range(TAPS_PER_PHASE)]
        g = sum(taps)
        taps = [t / g for t in taps]           # unity DC gain per phase
        phases.append(taps)
    return phases


def emit(phases, L, macro):
    lines = [f"/* L={L}, {TAPS_PER_PHASE} taps/phase, Kaiser-windowed sinc, per-phase unity gain. */",
             f"static const float {macro}[{L}][DSP_TAPS_PER_PHASE] = {{"]
    for taps in phases:
        body = ", ".join(f"{t: .10f}f" for t in taps)
        lines.append(f"    {{ {body} }},")
    lines.append("};")
    return "\n".join(lines)


if "--x2" in sys.argv:
    # CR-012 (AUDIO_OUT_I2S16K): CVSD 8k->16k. fc 3.8 kHz keeps 3.4 kHz flat and
    # suppresses the image from 4.6 kHz up.
    print(emit(design(2, 3800.0, 7.0, "up2", 16000), 2, "s_coef_x2"))
    sys.exit(0)

# CVSD 8k->48k: passband must stay flat to 3.4 kHz (narrowband speech).
p6 = design(6, 3950.0, 7.0, "up6")
# mSBC 16k->48k: flat to ~7 kHz.
p3 = design(3, 7400.0, 7.0, "up3")
print(f"#define DSP_TAPS_PER_PHASE {TAPS_PER_PHASE}")
print()
print(emit(p6, 6, "s_coef_x6"))
print()
print(emit(p3, 3, "s_coef_x3"))
