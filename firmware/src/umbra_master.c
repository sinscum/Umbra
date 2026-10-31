/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA MASTER (docs/DESIGN.md §4): after the master level, before the base tree's limiter and soft clip (fx.c
 * master_out), on the stereo mix:
 *   DJ FILTER  one knob: below 0 a low-pass falling from 16 kHz to ~110 Hz, above 0 a high-pass rising from 30 Hz
 *              to ~4 kHz (the engines' trapezoidal state-variable filter, a little resonance)
 *   GLUE       a gentle compressor on both sides together (one envelope): threshold -3 .. -11 dBFS, ratio 2:1 .. 4:1,
 *              ~12 ms attack, ~190 ms release, some make-up; the gain set once a block (one divide), ramped across it
 * Both at 0 leave the mix bit for bit as it was (the golden renders); each keeps running after its knob reaches 0
 * until it is transparent (the gain back at 1, the cutoff at its end), so turning it off never clicks. */
#define DJ_K 5000                                       /* damping Q12: Q ~0.8 */
#define DJ_LP_END (127 << 8)                            /* the LP's open end (16 kHz), the HP's (30 Hz): 0 */

typedef struct { int32_t ic1, ic2; } dj_side_t;
static struct {
    uint8_t dj_on, gl_on;
    int8_t hp;                  /* the filter running: 0 LP, 1 HP */
    int32_t cut;                /* its cutoff, 0 .. 127 << 8 (glides) */
    tsvf_t c;                   /* its coefficients for the block */
    dj_side_t s[2];
    int32_t env;                /* GLUE: the envelope (both sides' peak) */
    int32_t cg, g, gs;          /* .. the block's gain (Q12), the sample's, its step */
} umst;

/* once a block, before its samples (fx.c mix_block / perf_master); 1 = DJ FILTER or GLUE running this block (then
 * umaster_mix renders the master in place of fx.c's own loop: that loop, the ISR's, stays as it was) */
static __attribute__((noinline)) int umaster_block(uint32_t n)
{
    int32_t dj = usong[US_DJ], glue = usong[US_GLUE];
    if (dj || umst.dj_on) {                               /* DJ FILTER */
        int hp = dj > 0 || (dj == 0 && umst.hp);
        int32_t tgt = dj < 0 ? DJ_LP_END + dj * ((127 - 36) << 8) / 64 : dj > 0 ? dj * (90 << 8) / 63 : hp ? 0 : DJ_LP_END;
        if (umst.dj_on && hp != umst.hp) {              /* the other side: this one opens first, then the switch */
            int32_t end = umst.hp ? 0 : DJ_LP_END;
            if (umst.hp ? umst.cut > 16 : umst.cut < DJ_LP_END - 16) {
                hp = umst.hp;
                tgt = end;
            }
        }
        if (!umst.dj_on || hp != umst.hp) {                 /* a start (or a side change, at the open end): from there */
            memset(umst.s, 0, sizeof umst.s);
            umst.hp = (int8_t)hp;
            umst.cut = hp ? 0 : DJ_LP_END;
            umst.dj_on = 2;                               /* (2: the LP's state takes the first sample: no step) */
        }
        umst.cut += (tgt - umst.cut) / 4;
        if (!dj && (hp ? umst.cut < 16 : umst.cut > DJ_LP_END - 16))
            umst.dj_on = 0;                               /* back at its open end: off */
        tsvf_coef_k(&umst.c, umst.cut, DJ_K);
    }
    if (glue || umst.gl_on) {                             /* GLUE */
        int32_t th = 24000 - glue * 120, want = 4096, old;
        if (!umst.gl_on) {
            umst.gl_on = 1;
            umst.cg = 4096;
        }
        old = umst.cg;
        if (glue && umst.env > th) {
            int32_t over = umst.env - th, ratio = 2 + glue / 64;
            want = ((th + over / ratio) << 12) / umst.env;   /* (env > th >= 8760: 32 bits, no 64-bit divide) */
        }
        want = (want * (4096 + glue * 6)) >> 12;        /* make-up: up to +1.5 dB */
        umst.cg += (want - umst.cg) / 4;
        if (!glue && umst.cg > 4092 && umst.cg < 4100)
            umst.cg = 4096;
        umst.g = old;                                     /* (the block from the last one's gain to this one's) */
        umst.gs = (umst.cg - old) / (int32_t)n;
        if (!glue && umst.cg == 4096 && old == 4096)
            umst.gl_on = 0;
    }
    return umst.dj_on || umst.gl_on;
}

static inline int32_t dj_run(dj_side_t *s, int32_t x)
{
    int32_t v3, v1, v2, y;
    x = clamp(x, -262143, 262143) >> 1;
    v3 = x - s->ic2;
    v1 = (umst.c.a1 * s->ic1 + umst.c.a2 * v3) >> 13;
    v2 = s->ic2 + ((umst.c.a2 * s->ic1 + umst.c.a3 * v3) >> 13);
    s->ic1 = clamp(2 * v1 - s->ic1, -150000, 150000);
    s->ic2 = clamp(2 * v2 - s->ic2, -150000, 150000);
    y = umst.hp ? x - ((DJ_K * v1) >> 12) - v2 : v2;
    return y * 2;
}

/* each sample, before master_out */
static inline void umaster(int32_t *l, int32_t *r)
{
    if (umst.dj_on) {
        if (umst.dj_on == 2u) {                           /* (the LP's state at the signal: it starts transparent) */
            umst.s[0].ic2 = umst.hp ? 0 : clamp(*l, -262143, 262143) >> 1;
            umst.s[1].ic2 = umst.hp ? 0 : clamp(*r, -262143, 262143) >> 1;
            umst.dj_on = 1;
        }
        *l = dj_run(&umst.s[0], *l);
        *r = dj_run(&umst.s[1], *r);
    }
    if (umst.gl_on) {
        int32_t al = *l < 0 ? -*l : *l, ar = *r < 0 ? -*r : *r, a = al > ar ? al : ar;
        umst.env += a > umst.env ? (a - umst.env) >> 9 : -((umst.env - a) >> 13);
        umst.g += umst.gs;
        *l = (int32_t)(((int64_t)*l * umst.g) >> 12);
        *r = (int32_t)(((int64_t)*r * umst.g) >> 12);
    }
}

/* the master's samples with DJ FILTER / GLUE (fx.c: mix_block's loop, or perf_master's when wet is 0: the mix already
 * levelled), each through master_out as fx.c's loop does. Out of line: its cost is its own, the ISR's loop unchanged
 * (tests/target_budget.py) */
static inline void master_out(int32_t *l, int32_t *r);
static __attribute__((noinline)) void umaster_mix(int32_t *out, const int32_t *ml, const int32_t *mr, const int32_t *wet,
                                                  int32_t mg, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l = wet ? (((ml[i] + wet[i]) >> 2) * mg) >> 10 : ml[i];
        int32_t r = wet ? (((mr[i] + wet[i]) >> 2) * mg) >> 10 : mr[i];
        umaster(&l, &r);
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
