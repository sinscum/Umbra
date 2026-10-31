#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""UMBRA GRANULAR's tables -> firmware/src/umbra_grn_tab.h (checked in; run again after changing a window).
G.711 mu-law decode (256), the four grain windows (Q15, 257 points): HANN, TRI, PERC, REV."""
import math, os

def dec(u):
    u = (~u) & 0xFF
    t = (((u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4)
    return 0x84 - t if u & 0x80 else t - 0x84

def win(c, x):
    if c == 0:
        return math.sin(math.pi * x) ** 2                       # HANN
    if c == 1:
        return 1 - abs(2 * x - 1)                               # TRI
    if c == 2:
        return x / 0.03 if x < 0.03 else ((1 - x) / 0.97) ** 2  # PERC: 3 % up, then (1 - x)^2
    return x / 0.96 if x < 0.96 else (1 - x) / 0.04             # REV: rising, 4 % down

out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
       "/* UMBRA GRANULAR's tables (umbra_granular.c), in flash: written by tools/gen_grn_tab.py (G.711 mu-law decode;",
       " * the four windows, Q15, 257 points over a grain). Do not edit by hand. */",
       "#ifndef UMBRA_GRN_TAB_H", "#define UMBRA_GRN_TAB_H", "static const int16_t GRN_ULAW[256] = {"]
v = [dec(u) for u in range(256)]
out += ["    " + ", ".join(str(a) for a in v[i:i + 12]) + "," for i in range(0, 256, 12)]
out += ["};", "static const uint8_t GRN_ULAW_EXP[256] = {   /* the encode's exponent of (m >> 7), G.711 */"]
e = [0 if i == 0 else min(7, i.bit_length() - 1) for i in range(256)]
out += ["    " + ", ".join(str(a) for a in e[i:i + 32]) + "," for i in range(0, 256, 32)]
out += ["};", "static const int16_t GRN_WIN[4][257] = {   /* HANN, TRI, PERC (3 % up, then (1 - x)^2), REV (rising, 4 % down) */"]
for c in range(4):
    w = [max(0, min(32767, int(round(win(c, k / 256) * 32767)))) for k in range(257)]
    out += ["    {"] + ["        " + ", ".join(str(a) for a in w[i:i + 14]) + "," for i in range(0, 257, 14)] + ["    },"]
out += ["};", "#endif"]
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "..", "firmware", "src", "umbra_grn_tab.h"), "w") as f:
    f.write("\n".join(out) + "\n")
