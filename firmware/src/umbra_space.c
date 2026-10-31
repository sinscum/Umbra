/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA SPACE (docs/DESIGN.md §3.5): the shared delay and reverb buses (fx.c fx_buses, the base tree's), with
 *   WOW      the delay's read point drifting (two slow sines): tape wow on the echoes, up to ~5 ms (~30 cents)
 *   SHIMMER  an octave up of the reverb fed back into it (UMBRA_SHIMMER: off by default, measured first)
 *   PRE-DLY  the reverb's send delayed 0 .. 100 ms (at 22.05 kHz, pairs of samples averaged: 4.4 KB)
 *   FREEZE   both buses' inputs closed and their loops held at unity: the tails hang until it is let go
 * The values are the song's, not a track's (usong[], with MASTER's DJ FILTER and GLUE: umbra_master.c), saved in
 * UMB1 v3 beside the chain (umbra_store.c); FREEZE is never saved (a performance control).
 * Each at 0 / OFF leaves the buses bit for bit the base tree's (the golden renders).
 * FREEZE in each reverb model: ROOM's combs at feedback 1 with no damping (lossless: integer copies, as Freeverb's
 * freeze), HALL's eight lines at the gain that makes its loop unity (1 / sqrt 7: 6192.67 Q14, so 6193 and 6192 in
 * turn, 11 blocks in 16) with no damping and the gains rounded to the nearest (a slow fade of the highs remains: the
 * drifting taps read between samples), the delay at feedback 1 with no colour (lossless); SPRING only closes its
 * input (it rings out). Engage and release glide over ~17 ms (FRZ_STEP): no click. */
#ifndef UMBRA_SPACE
#define UMBRA_SPACE UMBRA_CHAIN
#endif
#ifndef UMBRA_SHIMMER
#define UMBRA_SHIMMER 0
#endif

enum { US_WOW, US_SHIM, US_PRE, US_FRZ, US_DJ, US_GLUE, US_N = 8 };   /* (usong's order: stored, UMB1 v3) */
static const param_desc_t US_PD[US_N] = {
    {"WOW", F_PCT, 0, 127, 0, 0, 0},
#if UMBRA_SHIMMER
    {"SHIM", F_PCT, 0, 127, 0, 0, 0},
#else
    {"SHIM", F_PCT, 0, 0, 0, 0, 0},                       /* (not in this build: stays 0) */
#endif
    {"PRE", F_INT, 0, 100, 0, 0, "ms"},
    {"FRZ", F_ONOFF, 0, 1, 0, 0, 0},
    {"DJ", F_BIPCT, -64, 63, 0, 0, 0},                    /* MASTER: LP <- 0 -> HP (umbra_master.c) */
    {"GLUE", F_PCT, 0, 127, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
};
static volatile int16_t usong[US_N];                       /* the UI writes, the audio reads (one writer) */
static void usong_reset(void)
{
    uint32_t i;
    for (i = 0; i < US_N; i++)
        usong[i] = US_PD[i].def;
}
static volatile uint8_t usolo;                             /* MIXER's solos (bit t: track t), never saved */
/* params.c page_desc, scope SC_USONG: value id */
static const param_desc_t *usong_desc(uint32_t id, int16_t **valp)
{
    if (id >= US_N || US_PD[id].label[0] == '-') {
        *valp = 0;
        return 0;
    }
    *valp = (int16_t *)&usong[id];
    return &US_PD[id];
}

#define FRZ_ONE 32768
#define FRZ_STEP (FRZ_ONE / 24)                            /* 24 blocks of CTL: ~17 ms */
#define HL_G_FRZ(c) (((c) & 15u) < 11u ? 6193 : 6192)      /* HALL's unity line gain, 6192.67 on average */
#define PRE_FS2 22050u
#define PRE_LEN (UMBRA_MEM_PRE / 2u)

static int16_t spc_pre[PRE_LEN] __attribute__((section(".pool")));
#if UMBRA_SHIMMER
#define SHIM_LEN 1024u
static int16_t spc_shim[SHIM_LEN] __attribute__((section(".pool")));
#endif

static struct {
    int32_t fz, fz0;            /* FREEZE: the amount now (Q15, 0 .. FRZ_ONE) and at the block's start */
    uint32_t fzc;               /* HALL's gain alternation */
    uint32_t wph;               /* WOW's phase */
    int32_t wd;                 /* WOW's depth, Q8 samples (glides) */
    uint32_t pw;                /* PRE-DLY: the write index (pairs) */
    int32_t pl;                 /* .. the length now (pairs; glides to the value's) */
    int32_t pp;                 /* .. the last pair read (the odd samples interpolate from it) */
#if UMBRA_SHIMMER
    uint32_t sw, sph;           /* SHIMMER: write index, the taps' phase */
    int32_t sfb[CTL];           /* .. its output, into the next block's send */
#endif
} spc;

static void space_reset(void)
{
    uint32_t i;
    memset(&spc, 0, sizeof spc);
    for (i = 0; i < PRE_LEN; i++)
        spc_pre[i] = 0;
#if UMBRA_SHIMMER
    for (i = 0; i < SHIM_LEN; i++)
        spc_shim[i] = 0;
#endif
}

/* the block's FREEZE amount: towards 1 while FRZ is on, towards 0 after (fx_buses and the reverbs read spc.fz) */
static inline void space_freeze_step(void)
{
    int32_t t = usong[US_FRZ] ? FRZ_ONE : 0;
    spc.fz0 = spc.fz;
    spc.fz = spc.fz < t ? (spc.fz + FRZ_STEP > t ? t : spc.fz + FRZ_STEP) : (spc.fz - FRZ_STEP < t ? t : spc.fz - FRZ_STEP);
    spc.fzc++;
}
/* a block value v moved towards its frozen value f by the freeze amount (v itself when not frozen) */
static inline int32_t frz_mix(int32_t v, int32_t f) { return spc.fz ? v + (int32_t)(((int64_t)(f - v) * spc.fz) >> 15) : v; }

/* before the buses (fx.c mix_block): SHIMMER's return and PRE-DLY into the reverb's send, FREEZE closing both sends */
static __attribute__((noinline)) void space_pre(int32_t *dly_in, int32_t *rev_in, uint32_t n)
{
    uint32_t i;
    int32_t want;
    space_freeze_step();
#if UMBRA_SHIMMER
    if (usong[US_SHIM])
        for (i = 0; i < n; i++)
            rev_in[i] += spc.sfb[i];
#endif
    want = usong[US_PRE] * (int32_t)PRE_FS2 / 1000;           /* pairs: 0 .. 2205 */
    if (want || spc.pl) {                                     /* (0 and arrived: the send as it is) */
        uint32_t j;
        spc.pl += want > spc.pl ? 1 : want < spc.pl ? -1 : 0;   /* a pair a block: ~6 % of pitch while it moves */
        for (j = 0; j + 1u < n; j += 2u) {
            int32_t x = (rev_in[j] + rev_in[j + 1u]) >> 1, d;
            uint32_t r = spc.pw >= (uint32_t)spc.pl ? spc.pw - (uint32_t)spc.pl : spc.pw + PRE_LEN - (uint32_t)spc.pl;
            spc_pre[spc.pw] = (int16_t)clamp(x >> 2, -32768, 32767);
            d = spc_pre[r] * 4;
            rev_in[j] = (spc.pp + d) >> 1;
            rev_in[j + 1u] = d;
            spc.pp = d;
            spc.pw = spc.pw + 1u >= PRE_LEN ? 0u : spc.pw + 1u;
        }
    }
    if (spc.fz || spc.fz0) {                                  /* FREEZE: the sends closed (glided across the block) */
        int32_t g = FRZ_ONE - spc.fz0, gs = ((FRZ_ONE - spc.fz) - g) / (int32_t)n;
        for (i = 0; i < n; i++, g += gs) {
            dly_in[i] = (int32_t)(((int64_t)dly_in[i] * g) >> 15);
            rev_in[i] = (int32_t)(((int64_t)rev_in[i] * g) >> 15);
        }
    }
}

/* the delay's read: d samples behind w (fx.c fx_buses), drifting with WOW (between samples, linear) */
static __attribute__((noinline)) int32_t space_dly_read(const int16_t *buf, uint32_t len, uint32_t w, uint32_t d)   /* (out of
                                                                     * line: the delay's loop stays as it was) */
{
    int32_t s = osc_sine(spc.wph) + (osc_sine(spc.wph * 3u + 0x40000000u) >> 2);   /* -1.25 .. 1.25, Q15 */
    int32_t q = (spc.wd + (int32_t)(((int64_t)spc.wd * s) >> 15) * 4 / 5);     /* 0 .. 2 wd, Q8 */
    uint32_t r = (d << 8) + (uint32_t)q, ri = r >> 8;
    int32_t f = (int32_t)(r & 255u), c0, c1;
    if (ri >= len - 2u)
        ri = len - 2u;
    c0 = buf[(w - ri) & (len - 1u)];
    c1 = buf[(w - ri - 1u) & (len - 1u)];
    spc.wph += 53567u;                                       /* 0.55 Hz (and three times it, 1.65 Hz) */
    return c0 + (((c1 - c0) * f) >> 8);
}
static __attribute__((noinline)) int space_wow_on(void)
{
    int32_t t = (usong[US_WOW] * usong[US_WOW] * 7) >> 1;      /* Q8 samples: 0 .. ~220 */
    if (spc.wd != t)
        spc.wd += t > spc.wd ? (t - spc.wd > 64 ? 64 : t - spc.wd) : -(spc.wd - t > 64 ? 64 : spc.wd - t);
    return spc.wd != 0;
}

#if UMBRA_SHIMMER
/* SHIMMER: the reverb's block (r, mono) an octave up (two taps sweeping a short line at twice the speed, half a sweep
 * apart, cross-faded by triangles), scaled into spc.sfb for the next block's send. Cost: tests/space_test.c */
#define SHIM_LO 64u
#define SHIM_P 896u                                          /* the sweep, samples */
static __attribute__((noinline)) void space_shimmer(const int32_t *r, uint32_t n)
{
    int32_t amt = usong[US_SHIM] * 180;                      /* Q15: 0 .. 0.7 */
    uint32_t i;
    if (!amt) {
        memset(spc.sfb, 0, sizeof spc.sfb);
        return;
    }
    for (i = 0; i < n; i++) {
        uint32_t p1 = spc.sph, p2 = (spc.sph + SHIM_P / 2u) % SHIM_P;
        uint32_t d1 = SHIM_LO + SHIM_P - p1, d2 = SHIM_LO + SHIM_P - p2;
        int32_t g1 = (int32_t)(p1 < SHIM_P / 2u ? p1 : SHIM_P - p1) * 2 * 32767 / (int32_t)SHIM_P, y;
        spc_shim[spc.sw & (SHIM_LEN - 1u)] = (int16_t)clamp(r[i] >> 2, -32768, 32767);
        y = (spc_shim[(spc.sw - d1) & (SHIM_LEN - 1u)] * g1 + spc_shim[(spc.sw - d2) & (SHIM_LEN - 1u)] * (32767 - g1)) >> 15;
        spc.sfb[i] = (int32_t)(((int64_t)softclip(clamp(y, -65535, 65535)) * 4 * amt) >> 15);
        spc.sw++;
        spc.sph = spc.sph + 1u >= SHIM_P ? 0u : spc.sph + 1u;   /* (the read point gains a sample a sample: 2x) */
    }
}
#endif
