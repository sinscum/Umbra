/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA FILTER (docs/DESIGN.md §3.3): the track's block after SOURCE / GRANULAR (umbra_chain.c chain_pre), before
 * DRIVE. Types (FILTER's THRU, index 0, is the chain's): SVF here; COMB, RESONATOR, FORMANT next.
 *
 * SVF: the engines' trapezoidal state-variable filter (dsp.c tsvf: Simper's, unconditionally stable) with all three
 * outputs, MORPH crossfading LP -> BP -> HP -> NOTCH; MIX dry / wet. The cutoff: FREQ (0..127, 30 Hz .. 16 kHz, the
 * base tree's scale: ~1.18 steps a semitone), plus KEYTRK of the newest note's distance from C4, plus WANDER (a slow
 * random drift, a new aim every ~1.5 s, glided to); set once a block (one divide), glided (no zipper). The block is
 * halved into the filter and doubled out (the filter's state stays within its +-150000; loud chords reach 2^18).
 * A ringing filter keeps the track's mix running after its voices end (fx.c mix_part: chain_ringing). */
enum { FILTER_THRU, FILTER_SVF };                       /* (DEV_T_FILTER's order: stored) */
enum { SVF_FREQ, SVF_RES, SVF_MORPH, SVF_MIX, SVF_KTRK, SVF_WANDER };

static const param_desc_t FLT_PD_SVF[8] = {
    {"FREQ", F_CUTOFF, 0, 127, 100, 0, 0},
    {"RES", F_PCT, 0, 127, 30, 0, 0},
    {"MRPH", F_PCT, 0, 127, 0, 0, 0},
    {"MIX", F_PCT, 0, 127, 127, 0, 0},
    {"KTRK", F_PCT, 0, 127, 0, 0, 0},
    {"WNDR", F_PCT, 0, 127, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
};
#define FLT_T_SVF {"SVF", 60, FLT_PD_SVF}

typedef struct {
    uint8_t on;                 /* ran last block (else: a fresh start) */
    int32_t cut;                /* the cutoff in use, 0..127 << 8 */
    int32_t ic1, ic2;           /* the SVF's state */
    int32_t wt, wv;             /* WANDER: its aim, where it is (Q15 -1..1) */
    uint32_t wn;                /* .. blocks to the next aim */
    int32_t rng;
} flt_state_t;
static flt_state_t flt_st[NTRK];

/* the newest sounding voice's note (KEYTRK), 60 with none */
static uint32_t flt_note(const track_t *t)
{
    uint32_t i, best = 60, age = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].age >= age) {
            age = t->v[i].age;
            best = t->v[i].note;
        }
    return best;
}

/* the cutoff a block aims at, 0..127 << 8 */
static int32_t flt_cut_target(flt_state_t *s, const track_t *t, const int16_t *p)
{
    int32_t c = p[SVF_FREQ] << 8;
    if (p[SVF_KTRK])                                    /* ~302 a semitone at 100 % (1.18 steps of the scale) */
        c += ((int32_t)flt_note(t) - 60) * 302 * p[SVF_KTRK] / 127;
    if (p[SVF_WANDER]) {                                /* up to +-24 steps (~1.7 octaves) at its top */
        if (!s->wn--) {
            s->wn = 2067u;                              /* ~1.5 s of blocks */
            s->wt = (int32_t)(noise32(&s->rng) >> 17) - 16384 + (int32_t)(noise32(&s->rng) >> 17) - 16384;
        }
        s->wv += (s->wt - s->wv) >> 9;                  /* glided: ~0.4 s */
        c += ((s->wv * p[SVF_WANDER] * 24) >> 15) * 2;   /* (Q15 x 0..127 x 24 steps, << 8 / 127) */
    }
    return clamp(c, 0, 127 << 8);
}

static void flt_svf(flt_state_t *s, const track_t *t, const int16_t *p, int32_t *b, uint32_t n)
{
    tsvf_t c;
    int32_t tgt = flt_cut_target(s, t, p), k = 8192 - p[SVF_RES] * 7600 / 127;
    int32_t m = p[SVF_MORPH] * 3, seg = m / 127, fr = (m - seg * 127) * 258;   /* MORPH: the pair, Q15 between */
    int32_t mix = p[SVF_MIX], mq = mix * 258;
    uint32_t i;
    if (!s->on) {
        int32_t r = s->rng ? s->rng : 0x1F2E3D4C;
        memset(s, 0, sizeof *s);
        s->on = 1;
        s->cut = tgt;
        s->rng = r;
    }
    s->cut += (tgt - s->cut) >> 2;                      /* glided, a block at a time */
    tsvf_coef_k(&c, s->cut, k);
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -262143, 262143) >> 1, v3 = x - s->ic2, lp, bp, hp, o[4], y;
        int32_t v1 = (c.a1 * s->ic1 + c.a2 * v3) >> 13;
        int32_t v2 = s->ic2 + ((c.a2 * s->ic1 + c.a3 * v3) >> 13);
        s->ic1 = clamp(2 * v1 - s->ic1, -150000, 150000);
        s->ic2 = clamp(2 * v2 - s->ic2, -150000, 150000);
        lp = v2;
        bp = v1;
        hp = x - ((k * bp) >> 12) - lp;
        o[0] = lp; o[1] = bp; o[2] = hp; o[3] = lp + hp;
        y = seg >= 3 ? o[3] : o[seg] + (int32_t)(((int64_t)(o[seg + 1] - o[seg]) * fr) >> 15);
        y = clamp(y, -131071, 131071) << 1;
        b[i] = mix >= 127 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * mq) >> 15);
    }
}

/* the filter still sounds (its state above the noise): the track's mix runs on after its voices */
static int flt_ringing(const flt_state_t *s)
{
    return s->on && (s->ic1 > 16 || s->ic1 < -16 || s->ic2 > 16 || s->ic2 < -16);
}
