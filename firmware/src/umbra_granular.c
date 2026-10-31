/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA GRANULAR, type GRAINS (docs/DESIGN.md §3.2): a cloud of short grains read from a ring that keeps recording the
 * track's SOURCE (fx.c mix_part -> umbra_chain.c chain_pre, before FILTER).
 *   the ring   a track's last 0.51 s at 22.05 kHz, 8-bit mu-law (UMBRA_GRN_RING bytes, umbra_mem.h): a value a pair of
 *              samples, written as the pair's average; read between values (linear)
 *   the pool   12 grains for all tracks (8 above ~80 % CPU), 6 at most a track; a new grain with none free fades the
 *              oldest out (64 samples) and waits for its slot: no click, the cloud thins under load
 *   a grain    2 reads + an interpolation + its window (HANN, TRI, PERC, REV: computed, no table) + a gain, a sample
 * Values: SIZE DENSITY PITCH SPRAY / SCAN SPEED CONTOUR MIX / REVERSE PITCH-RAND SCALE DETUNE.
 *   SCAN   how far back the cloud reads (0: just behind the write, 127: the ring's far end)
 *   SPEED  64 the read point keeps its distance (live); lower it falls behind, higher it catches up (the cloud
 *          stretches or hurries through the ring, wrapping); 0 suspends it: the ring stops recording, the cloud hangs
 *   SCALE  each grain's pitch onto the track's scale (seq.c scale_mask), PITCH and PITCH RAND in semitones
 * Gains: the cloud's level kept near the source's: 1 / sqrt(the grains overlapping). Fixed point throughout; per
 * spawn one 32-bit divide at most. THRU (the default) leaves the block as it is. Out of line (noinline): fx.c
 * mix_part's own loops stay as they were (tests/target_budget.py). */
#ifndef UMBRA_GRN
#define UMBRA_GRN UMBRA_CHAIN
#endif
enum { GRN_SIZE, GRN_DENS, GRN_PITCH, GRN_SPRAY, GRN_SCAN, GRN_SPEED, GRN_CONT, GRN_MIX, GRN_REV, GRN_PRAND, GRN_SCALE,
       GRN_DTUN };
enum { CONT_HANN, CONT_TRI, CONT_PERC, CONT_REV, CONT_N };
static const char *const GRN_CONT_N[CONT_N] = {"HANN", "TRI", "PERC", "REV"};
static const param_desc_t GRN_PD[DEV_NP] = {
    {"SIZE", F_PCT, 0, 127, 60, 0, 0},
    {"DENS", F_PCT, 0, 127, 50, 0, 0},
    {"PTCH", F_SEMI, -24, 24, 0, 0, 0},
    {"SPRY", F_PCT, 0, 127, 20, 0, 0},
    {"SCAN", F_PCT, 0, 127, 30, 0, 0},
    {"SPED", F_PCT, 0, 127, 64, 0, 0},
    {"CONT", F_ENUM, 0, CONT_N - 1, CONT_HANN, GRN_CONT_N, 0},
    {"MIX", F_PCT, 0, 127, 127, 0, 0},
    {"REV", F_PCT, 0, 127, 0, 0, 0},
    {"PRND", F_INT, 0, 12, 0, 0, 0},
    {"SCAL", F_ONOFF, 0, 1, 0, 0, 0},
    {"DTUN", F_PCT, 0, 127, 0, 0, 0},
};
#define GRN_T_GRAINS {"GRAINS", 220, GRN_PD}

#include "umbra_grn_tab.h"
#define GRN_RL UMBRA_GRN_RING                   /* the ring's values (bytes) */
#define GRN_MAX 12u
#define GRN_MAX_LOAD 8u
#define GRN_PER_TRK 6u
#define GRN_FADE 64u
static uint8_t grn_ring[NTRK][GRN_RL] __attribute__((section(".pool")));

typedef struct {
    uint8_t on, tr, cont, fading;
    uint32_t pos;               /* Q16 values into the ring */
    int32_t inc;                /* Q16 values a sample (reverse: < 0) */
    uint32_t t, len;            /* samples played, of len */
    uint32_t xq, xstep;         /* the window's place, Q23 (0 .. 32768 << 8), its step a sample */
    int32_t gain;               /* Q15 */
    uint32_t fade;              /* fading: samples left */
} grain_t;
static grain_t grn[GRN_MAX];
static struct {
    uint8_t on;
    uint32_t w;                 /* the ring's write index (values) */
    int32_t acc;                /* the pair's first sample */
    uint32_t odd;               /* the next sample is a pair's second */
    uint32_t spawn;             /* the spawn accumulator (FS = a grain) */
    int32_t drift;              /* SPEED's wander from SCAN's distance (values, Q8) */
    int32_t rng;
    uint32_t filled;            /* values written since the start (the ring's valid part) */
} grs[NTRK];
static volatile uint8_t grn_cap = GRN_MAX;      /* GRN_MAX_LOAD under load (chain_pre) */

/* mu-law (G.711), of x Q15 / 2 */
static inline uint8_t ulaw_enc(int32_t x)
{
    uint32_t s = x < 0 ? 0x80u : 0u, m, e = 7u;
    if (s) x = -x;
    m = (uint32_t)(x > 32635 ? 32635 : x) + 0x84u;
    e = GRN_ULAW_EXP[(m >> 7) & 0xFFu];                 /* the top bit's place above bit 7 (umbra_grn_tab.h) */
    return (uint8_t)~(s | e << 4 | ((m >> (e + 3u)) & 0x0Fu));
}
static inline int32_t ulaw_dec(uint8_t u) { return GRN_ULAW[u]; }   /* (umbra_grn_tab.h) */

static const uint32_t GRN_SEMI_Q16[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772,
                                      123715};   /* 2^(k / 12), Q16 */
static int32_t grn_ratio_q16(int32_t semi)       /* 2^(semi / 12), Q16, semi -36 .. 36 */
{
    int32_t o = (semi + 120) / 12 - 10, k = semi - o * 12;
    uint32_t r = GRN_SEMI_Q16[k];
    return (int32_t)(o >= 0 ? r << o : r >> -o);
}

/* the ring: n samples of the track's block b in, a value a pair (the pair's average), mu-law */
static __attribute__((noinline)) void grn_record(uint32_t tr, const int32_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -65535, 65535) >> 1;
        if (!grs[tr].odd) {
            grs[tr].acc = x;
        } else {
            grn_ring[tr][grs[tr].w] = ulaw_enc((grs[tr].acc + x) >> 1);
            grs[tr].w = grs[tr].w + 1u >= GRN_RL ? 0u : grs[tr].w + 1u;
            if (grs[tr].filled < GRN_RL)
                grs[tr].filled++;
        }
        grs[tr].odd ^= 1u;
    }
}

/* grains of track tr playing now */
static __attribute__((noinline)) uint32_t grn_count(uint32_t tr, uint32_t *all)
{
    uint32_t g, n = 0, a = 0;
    for (g = 0; g < GRN_MAX; g++)
        if (grn[g].on) {
            a++;
            n += grn[g].tr == tr;
        }
    *all = a;
    return n;
}
/* no slot for a new grain: the oldest (of the track when it has its share, else of all) fades out */
static __attribute__((noinline)) void grn_steal(uint32_t tr, int own)
{
    uint32_t g, best = GRN_MAX, most = 0;
    for (g = 0; g < GRN_MAX; g++) {
        const grain_t *q = &grn[g];
        if (!q->on || q->fading || (own && q->tr != tr))
            continue;
        if (q->t + 1u > most) {
            most = q->t + 1u;
            best = g;
        }
    }
    if (best < GRN_MAX) {
        grn[best].fading = 1;
        grn[best].fade = GRN_FADE;
    }
}

/* a new grain for track tr with values p */
static __attribute__((noinline)) void grn_spawn(uint32_t tr, const track_t *t, const int16_t *p, uint32_t len)
{
    uint32_t g, r = noise32(&grs[tr].rng), span, back, need, limit;
    int32_t semi = p[GRN_PITCH], inc;
    grain_t *q;
    for (g = 0; g < GRN_MAX && grn[g].on; g++)
        ;
    if (g >= GRN_MAX)
        return;
    q = &grn[g];
    if (p[GRN_PRAND])                                   /* PITCH RAND: +- semitones */
        semi += (int32_t)((r >> 8) % (uint32_t)(2 * p[GRN_PRAND] + 1)) - p[GRN_PRAND];
    if (p[GRN_SCALE]) {                                 /* SCALE: down onto the track's scale */
        uint32_t m = scale_mask(t) & 0xFFFu, k;
        int32_t rel = ((semi % 12) + 12) % 12;
        for (k = 0; m && k < 12u && !((m >> rel) & 1u); k++) {
            rel = (rel + 11) % 12;
            semi--;
        }
    }
    semi = clamp(semi, -36, 36);
    inc = grn_ratio_q16(semi) >> 1;                     /* (the ring: a value a pair of samples) */
    if (p[GRN_DTUN])                                    /* DETUNE: +- up to ~35 cents */
        inc += (int32_t)(((int64_t)inc * ((int32_t)((r >> 16) & 255u) - 128) * p[GRN_DTUN]) >> 22);
    need = (uint32_t)(((uint64_t)(uint32_t)inc * len) >> 16) + len / 2u + 64u;   /* values a forward grain covers, and
                                                                                  * the write moving on meanwhile */
    limit = grs[tr].filled > need + 64u ? grs[tr].filled - need - 64u : 0u;   /* (the valid part, behind the write) */
    span = limit * (uint32_t)p[GRN_SCAN] / 127u;
    back = span + (uint32_t)(grs[tr].drift >> 8) % (limit + 1u);
    if (p[GRN_SPRAY])
        back += (r >> 4) % (limit * (uint32_t)p[GRN_SPRAY] / 127u + 1u);
    back = back > limit ? limit : back;
    back += need;
    if (back >= GRN_RL)
        back = GRN_RL - 1u;
    q->on = 1;
    q->tr = (uint8_t)tr;
    q->cont = (uint8_t)(p[GRN_CONT] % CONT_N);
    q->fading = 0;
    q->pos = (uint32_t)((grs[tr].w + GRN_RL - back) % GRN_RL) << 16;
    if ((uint32_t)(r & 127u) < (uint32_t)p[GRN_REV]) {  /* REVERSE: this grain backwards, from its end */
        inc = -inc;
        q->pos = (uint32_t)((grs[tr].w + GRN_RL - 64u) % GRN_RL) << 16;
        if (back > 64u + need)
            q->pos = (uint32_t)((grs[tr].w + GRN_RL - back + need) % GRN_RL) << 16;
    }
    q->inc = inc;
    q->t = 0;
    q->len = len;
    q->xq = 0;
    q->xstep = (32768u << 8) / (len | 1u);
    q->gain = 32767;
}

/* track tr's block b (n samples, its SOURCE): recorded, then the cloud over it, MIX of it */
static __attribute__((noinline)) void grn_run(uint32_t tr, const track_t *t, const int16_t *p, int32_t *b, uint32_t n)
{
    static int32_t wet[CTL];
    uint32_t i, g, len = 882u + (uint32_t)(p[GRN_SIZE] * p[GRN_SIZE]), all, mine;
    uint32_t dens = 50u + (uint32_t)(p[GRN_DENS] * p[GRN_DENS]) / 4u;   /* grains a second x100: 0.5 .. ~41 */
    int32_t ov, gain8;
    if (!grs[tr].on) {
        int32_t seed = grs[tr].rng ? grs[tr].rng : (int32_t)(0x1234567u * (tr + 1u));
        memset(&grs[tr], 0, sizeof grs[tr]);
        grs[tr].on = 1;
        grs[tr].rng = seed;
    }
    if (p[GRN_SPEED])                                   /* SPEED 0: suspended (no recording, the cloud hangs) */
        grn_record(tr, b, n);
    if (p[GRN_SPEED] && p[GRN_SPEED] != 64) {           /* the read point's wander: (64 - SPEED) / 64 of real time */
        grs[tr].drift += (64 - p[GRN_SPEED]) * (int32_t)n * 2;   /* (Q8 values: n / 2 values a block x 4 x 64 / 64) */
        if (grs[tr].drift < 0)
            grs[tr].drift += (int32_t)(GRN_RL << 8);
        if (grs[tr].drift >= (int32_t)(GRN_RL << 8))
            grs[tr].drift -= (int32_t)(GRN_RL << 8);
    }
    grs[tr].spawn += dens * n;                           /* a grain each FS x 100 / dens samples */
    while (grs[tr].spawn >= FS * 100u) {
        mine = grn_count(tr, &all);
        if (mine >= GRN_PER_TRK || all >= grn_cap) {     /* none free: the oldest fades, this one waits */
            grn_steal(tr, mine >= GRN_PER_TRK);
            grs[tr].spawn = FS * 100u;                   /* (no backlog: one waiting) */
            break;
        }
        grn_spawn(tr, t, p, len);
        grs[tr].spawn -= FS * 100u;
    }
    ov = (int32_t)(dens * len / (FS * 100u)) + 1;   /* (< 4083 x 17011: 32 bits) */
      /* grains overlapping: the cloud's gain 1 / sqrt */
    gain8 = (int32_t)(256u * 16u / (isqrt32((uint32_t)ov * 256u) | 1u));   /* (Q8: wet x gain within 32 bits) */
    gain8 = gain8 > 256 ? 256 : gain8;
    for (i = 0; i < n; i++)
        wet[i] = 0;
    for (g = 0; g < GRN_MAX; g++) {
        grain_t *q = &grn[g];
        const int16_t *wt;
        const uint8_t *ring = grn_ring[tr];
        uint32_t pos, xq, m, k;
        int32_t inc, fade;
        if (!q->on || q->tr != tr)
            continue;
        wt = GRN_WIN[q->cont % CONT_N];
        pos = q->pos, xq = q->xq, inc = q->inc;
        m = q->len - q->t < n ? q->len - q->t : n;      /* the samples it has left in this block */
        if (q->fading && q->fade < m)
            m = q->fade;
        fade = q->fading ? (int32_t)q->fade : -1;
        for (k = 0; k < m; k++) {                       /* (locals, 32 bits: a sample 2 reads, 3 table looks) */
            uint32_t ri = pos >> 16, rj = ri + 1u >= GRN_RL ? 0u : ri + 1u;
            int32_t a = GRN_ULAW[ring[ri]], c = GRN_ULAW[ring[rj]];
            int32_t x = a + (((c - a) * (int32_t)((pos >> 8) & 255u)) >> 8), w = wt[xq >> 15];
            if (fade >= 0)
                w = (w * fade--) >> 6;                  /* (stolen: down to 0 over GRN_FADE, 64) */
            wet[k] += (x * w) >> 15;
            pos = (uint32_t)((int32_t)pos + inc);
            if ((int32_t)pos < 0)
                pos += GRN_RL << 16;
            else if (pos >= (GRN_RL << 16))
                pos -= GRN_RL << 16;
            xq += q->xstep;
        }
        q->pos = pos, q->xq = xq, q->t += m;
        if (fade >= 0)
            q->fade = (uint32_t)fade;
        if (q->t >= q->len || (q->fading && !q->fade))
            q->on = 0;
    }
    if (p[GRN_MIX] >= 127) {                            /* the cloud (x2: the ring held half), at its gain, MIX */
        for (i = 0; i < n; i++)
            b[i] = (wet[i] * gain8) >> 7;
    } else {
        int32_t mix8 = (p[GRN_MIX] * 258) >> 7;         /* (0 .. 255) */
        for (i = 0; i < n; i++)
            b[i] += ((((wet[i] * gain8) >> 7) - b[i]) * mix8) >> 8;
    }
}

/* GRAINS off (another type, bypassed): its grains go, the ring starts afresh next time */
static __attribute__((noinline)) void grn_stop(uint32_t tr)
{
    uint32_t g;
    for (g = 0; g < GRN_MAX; g++)
        if (grn[g].tr == tr)
            grn[g].on = 0;
    grs[tr].on = 0;
}
/* the cloud still sounding (fx.c mix_part runs the track on: its grains, a suspended cloud) */
static __attribute__((noinline)) int grn_ringing(uint32_t tr)
{
    uint32_t all;
    return grs[tr].on && (grn_count(tr, &all) || grs[tr].filled);
}
