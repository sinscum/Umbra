/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA FILTER (docs/DESIGN.md §3.3): the track's block after SOURCE / GRANULAR (umbra_chain.c chain_pre), before
 * DRIVE. Types (FILTER's THRU, index 0, is the chain's): SVF, COMB, RESONATOR, FORMANT.
 *
 * SVF: the engines' trapezoidal state-variable filter (dsp.c tsvf: Simper's, unconditionally stable) with all three
 * outputs, MORPH crossfading LP -> BP -> HP -> NOTCH; MIX dry / wet. The cutoff: FREQ (0..127, 30 Hz .. 16 kHz, the
 * base tree's scale: ~1.18 steps a semitone), plus KEYTRK of the newest note's distance from C4, plus WANDER (a slow
 * random drift, a new aim every ~1.5 s, glided to); set once a block (one divide), glided (no zipper). The block is
 * halved into the filter and doubled out (the filter's state stays within its +-150000; loud chords reach 2^18).
 * A ringing filter keeps the track's mix running after its voices end (fx.c mix_part: chain_ringing). */
enum { FILTER_THRU, FILTER_SVF, FILTER_COMB, FILTER_RES, FILTER_FORMANT };   /* (DEV_T_FILTER's order: stored) */
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

/* a two-pole resonator (RESONATOR's bands, FORMANT's formants): y = b0 (x - x[-2]) + a1 y1 - a2 y2, zeros at DC and
 * Nyquist so its peak gain stays ~1 at any pitch; coefficients Q29 (a1 up to 2), products 64-bit (low notes need the
 * bits: a1 is near 2 there) */
typedef struct { int32_t y1, y2, a1, a2, b0; } res2_t;   /* (y1, y2: Q12) */
#define FLT_BANDS 6

typedef struct {
    uint8_t on;                 /* ran last block (else: a fresh start) */
    int32_t cut;                /* the cutoff in use, 0..127 << 8 */
    int32_t ic1, ic2;           /* the SVF's state */
    int32_t wt, wv;             /* WANDER: its aim, where it is (Q15 -1..1) */
    uint32_t wn;                /* .. blocks to the next aim */
    int32_t rng;
    int32_t ring;               /* the wet's peak, the last block (COMB, RESONATOR, FORMANT: chain_ringing) */
    uint32_t cw;                /* COMB: the write position */
    int32_t clp, cper;          /* .. its loop's low pass, its period in use (Q8 samples) */
    int32_t x1, x2;             /* RESONATOR, FORMANT: the input's last two */
    res2_t r[FLT_BANDS];
    int32_t bw[FLT_BANDS];      /* RESONATOR: each band's WANDER, 1/16 semitone (glided) */
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
        y = clamp(y, -131071, 131071) * 2;
        b[i] = mix >= 127 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * mq) >> 15);
    }
}

/* ---------------------------------------------------------------------------------------------- COMB --- *
 * A tuned feedback comb (DESIGN §3.3: metallic, bell-like on noise): y = x g + POL FDBK lp(y[-period]); NOTE the
 * pitch (its period, linear between samples), DAMP a low pass in the loop, POL + (all harmonics) or - (odd ones, an
 * octave down: hollow). The input is lowered as FDBK rises (the peaks stay ~ the input's); the loop saturates
 * (tanh), never runs away. KEYTRK, WANDER as SVF's (in semitones here) */
enum { CMB_NOTE, CMB_FDBK, CMB_DAMP, CMB_MIX, CMB_KTRK, CMB_WANDER, CMB_POL };
static const char *const CMB_POL_N[] = {"+", "-"};
static const param_desc_t FLT_PD_COMB[8] = {
    {"NOTE", F_NOTE, 30, 96, 57, 0, 0},
    {"FDBK", F_PCT, 0, 127, 90, 0, 0},
    {"DAMP", F_PCT, 0, 127, 40, 0, 0},
    {"MIX", F_PCT, 0, 127, 127, 0, 0},
    {"KTRK", F_PCT, 0, 127, 0, 0, 0},
    {"WNDR", F_PCT, 0, 127, 0, 0, 0},
    {"POL", F_ENUM, 0, 1, 0, CMB_POL_N, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
};
#define FLT_T_COMB {"COMB", 50, FLT_PD_COMB}
static int16_t flt_comb_buf[NTRK][UMBRA_COMB_LEN] __attribute__((section(".pool")));   /* (umbra_mem.h) */

/* WANDER's drift, Q15 -1..1, a step a block (the cutoff's aim: flt_cut_target's, shared) */
static int32_t flt_wander(flt_state_t *s, int32_t on)
{
    if (!on)
        return 0;
    if (!s->wn--) {
        s->wn = 2067u;
        s->wt = (int32_t)(noise32(&s->rng) >> 17) - 16384 + (int32_t)(noise32(&s->rng) >> 17) - 16384;
    }
    s->wv += (s->wt - s->wv) >> 9;
    return s->wv;
}
/* a pitch in 1/16 semitone: note + KEYTRK (100 %: the newest note's distance from C4) + WANDER (100 %: +-1 st) */
static int32_t flt_pitch16(flt_state_t *s, const track_t *t, int32_t note, int32_t ktrk, int32_t wander)
{
    int32_t p = note * 16;
    if (ktrk)
        p += ((int32_t)flt_note(t) - 60) * 16 * ktrk / 127;
    p += (flt_wander(s, wander) * wander * 16) >> 22;    /* (Q15 x 0..127 / 2^7: +-16 at the top) */
    return clamp(p, 16 * 24, 16 * 120);
}

static void flt_comb(flt_state_t *s, const track_t *t, const int16_t *p, int32_t *b, uint32_t n, int16_t *buf)
{
    int32_t fb = p[CMB_FDBK] * 248, damp = 32767 - p[CMB_DAMP] * 230, gin = 32767 - (fb >> 1);   /* FDBK to 0.96 */
    int32_t mix = p[CMB_MIX], mq = mix * 258, per, ps, pk = 0;
    uint32_t inc = pitch_inc((uint32_t)flt_pitch16(s, t, p[CMB_NOTE], p[CMB_KTRK], p[CMB_WANDER])), i;
    if (p[CMB_POL])
        fb = -fb;
    per = (int32_t)(0xFFFFFFFFu / ((inc >> 8) | 1u));   /* the period, Q8 samples (one divide a block) */
    per = clamp(per, 2 << 8, (int32_t)(UMBRA_COMB_LEN - 2u) << 8);
    if (!s->on) {
        int32_t r = s->rng ? s->rng : 0x1F2E3D4C;
        memset(s, 0, sizeof *s);
        memset(buf, 0, UMBRA_COMB_LEN * sizeof buf[0]);
        s->on = 1;
        s->rng = r;
        s->cper = per;
    }
    ps = (per - s->cper) / (int32_t)n;                  /* the period glides across the block */
    for (i = 0; i < n; i++) {
        uint32_t d = (uint32_t)s->cper >> 8, fr = (uint32_t)s->cper & 255u;
        uint32_t ia = (s->cw - d) & (UMBRA_COMB_LEN - 1u), ib = (s->cw - d - 1u) & (UMBRA_COMB_LEN - 1u);
        int32_t z = ((int32_t)buf[ia] * (int32_t)(256u - fr) + (int32_t)buf[ib] * (int32_t)fr) >> 8, x, y, a;
        s->cper += ps;
        s->clp += ((z - s->clp) * damp + 16384) >> 15;  /* DAMP: the loop's low pass */
        x = clamp(b[i], -262143, 262143) >> 2;          /* (a quarter in: the line is int16) */
        y = softclip(mulq15(x, gin) + mulq15(s->clp, fb));   /* (saturates: never runs away) */
        buf[s->cw] = (int16_t)y;
        s->cw = (s->cw + 1u) & (UMBRA_COMB_LEN - 1u);
        a = y < 0 ? -y : y;
        if (a > pk) pk = a;
        y <<= 2;
        b[i] = mix >= 127 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * mq) >> 15);
    }
    s->cper = per;
    s->ring = pk;
}

/* --------------------------------------------------------------------------------------- resonator bank --- */
/* the make-up of nb bands ringing for t60 (Q8), applied before them (their level stays well above their rounding): a
 * band of 1 - r = e keeps sqrt(e / 2) of white noise, so nb of them are lifted by 1 / sqrt(e nb / 2) / 2 (noise in:
 * about half its level out); clamped 1x .. 16x. A tone right on a band is louder than that: the band's state and the
 * tanh after the bands hold it (flt_bank_out) */
static int32_t res2_makeup(uint32_t t60x10, uint32_t nb)
{
    uint32_t e = 1681979000u / (t60x10 | 1u), q = isqrt32(e * nb);   /* (the engines' isqrt32, eng_noise.c;
                                                                         * e <= 2^28, nb <= 6: fits) */
    return clamp((int32_t)(128u * 46341u / (q | 1u)), 256, 256 * 16);
}
/* a bank's sum (quarter scale), saturated (tanh: the bank's top is half the block's), full scale */
static inline int32_t flt_bank_out(int32_t y)
{
    return softclip(clamp(y, -65535, 65535)) * 4;
}

/* a band at pitch16 (1/16 semitone) ringing for t60 (TIME_MS_X10's units: 0.1 ms) */
static void res2_coef(res2_t *r, uint32_t inc, uint32_t t60x10)
{
    int64_t w = (int64_t)(((uint64_t)inc * 1686629713u) >> 30), w2, c;   /* w: radians Q30 (2 pi inc / 2^32 = inc pi / 2) */
    int32_t e = (int32_t)(1681979000u / (t60x10 | 1u)), rr;            /* 1 - r, Q30: 6.908 / (T60 FS) */
    if (w > (1 << 30))
        w = 1 << 30;                                    /* (<= 1 rad: ~7 kHz; the series is good there) */
    w2 = (w * w) >> 30;
    c = (1 << 30) - (w2 >> 1) + (int32_t)((w2 * w2) >> 30) / 24;   /* cos w, Q30 (to w^4; a 32-bit divide) */
    rr = (1 << 30) - clamp(e, 1, 1 << 28);
    r->a1 = (int32_t)(((int64_t)rr * c) >> 30);          /* 2 r cos w, Q29 (r c in Q30 is 2 r c in Q29) */
    r->a2 = (int32_t)(((int64_t)rr * rr) >> 31);         /* r^2, Q29 */
    r->b0 = ((1 << 29) - r->a2) >> 1;                    /* (1 - r^2) / 2, Q29: unity at the peak */
}
static inline int32_t res2_run(res2_t *r, int32_t xd)   /* xd = x - x[-2] (lifted by the make-up: res2_makeup);
                                                         * the state Q12 (12 bits below the signal) */
{
    int64_t a = (int64_t)r->b0 * ((int64_t)xd * 4096) + (int64_t)r->a1 * r->y1 - (int64_t)r->a2 * r->y2;
    int32_t y = (int32_t)((a + (1 << 28)) >> 29);      /* (rounded: no bias, which near DC a pole's gain of ~1e5
                                                         * would grow to an offset; the make-up before the band keeps
                                                         * its level well above the rounding, whose leftovers stay
                                                         * under a unit out) */
    y = clamp(y, -(1 << 30), 1 << 30);                  /* (+-262143 out: a tone right on a band saturates) */
    r->y2 = r->y1;
    r->y1 = y;
    return (y + 2048) >> 12;
}

/* ------------------------------------------------------------------------------------------ RESONATOR --- *
 * Six tuned resonators (DESIGN §3.3: drones out of noise): ROOT a note, TUNE the chord they stand on (or the track's
 * own scale from ROOT), MORPH 0 every band on ROOT .. 64 the chord .. 127 twice as wide, RING how long they ring,
 * SPREAD a detune between them, WANDER each band drifting on its own, KEYTRK ROOT follows the notes. Coefficients a
 * block, a divide each; under load (the CPU meter over ~80 %) four bands */
enum { RSN_ROOT, RSN_RING, RSN_MORPH, RSN_MIX, RSN_TUNE, RSN_SPREAD, RSN_WANDER, RSN_KTRK };
enum { TUNE_MINOR, TUNE_MAJOR, TUNE_SUS4, TUNE_FIFTHS, TUNE_OCTAVES, TUNE_SCALE };
static const char *const RSN_TUNE_N[] = {"MINOR", "MAJOR", "SUS4", "FIFTHS", "OCTAVE", "SCALE"};
static const int8_t RSN_CHORD[5][FLT_BANDS] = {
    {0, 3, 7, 12, 15, 19}, {0, 4, 7, 12, 16, 19}, {0, 5, 7, 12, 17, 19}, {0, 7, 12, 19, 24, 31}, {0, 12, 24, 7, 19, 36}};
static const param_desc_t FLT_PD_RES[8] = {
    {"ROOT", F_NOTE, 24, 84, 45, 0, 0},
    {"RING", F_PCT, 0, 127, 80, 0, 0},
    {"MRPH", F_PCT, 0, 127, 64, 0, 0},
    {"MIX", F_PCT, 0, 127, 127, 0, 0},
    {"TUNE", F_ENUM, 0, 5, 0, RSN_TUNE_N, 0},
    {"SPRD", F_PCT, 0, 127, 0, 0, 0},
    {"WNDR", F_PCT, 0, 127, 0, 0, 0},
    {"KTRK", F_PCT, 0, 127, 0, 0, 0},
};
#define FLT_T_RES {"RESONATR", 160, FLT_PD_RES}
static uint32_t scale_mask(const track_t *t);          /* (seq.c: the track's scale, bit k: k semitones above ROOT) */
static volatile uint8_t flt_res_bands = FLT_BANDS;      /* 6, or 4 under load (fx.c / audio.c: the CPU meter) */

/* band k's interval from ROOT in semitones (TUNE) */
static int32_t rsn_interval(const track_t *t, uint32_t tune, uint32_t k, int32_t root)
{
    if (tune < TUNE_SCALE)
        return RSN_CHORD[tune][k];
    {   /* the scale, from ROOT: its 1st 3rd 5th 8th 10th 12th degrees */
        static const uint8_t DEG[FLT_BANDS] = {0, 2, 4, 7, 9, 11};
        uint32_t mask = scale_mask(t), rel = (uint32_t)(root - t->p[P_ROOT] + 120) % 12u, n = 0, st;
        if (!(mask & 0xFFFu))
            mask = 0xFFFu;
        for (st = 0; st < 48u; st++)
            if ((mask >> ((rel + st) % 12u)) & 1u) {
                if (n == DEG[k])
                    return (int32_t)st;
                n++;
            }
        return 0;
    }
}

static void flt_res(flt_state_t *s, const track_t *t, const int16_t *p, int32_t *b, uint32_t n)
{
    uint32_t nb = flt_res_bands, k, i, t60 = TIME_MS_X10[clamp(40 + p[RSN_RING] * 87 / 127, 0, 127)];
    int32_t root16 = flt_pitch16(s, t, p[RSN_ROOT], p[RSN_KTRK], 0), mix = p[RSN_MIX], mq = mix * 258, pk = 0;
    int32_t mk = res2_makeup(t60, nb);
    if (!s->on) {
        int32_t r = s->rng ? s->rng : 0x1F2E3D4C;
        memset(s, 0, sizeof *s);
        s->on = 1;
        s->rng = r;
    }
    for (k = 0; k < nb; k++) {                          /* each band's pitch, a block */
        int32_t iv = rsn_interval(t, (uint32_t)p[RSN_TUNE], k, p[RSN_ROOT]) * 16 * p[RSN_MORPH] / 64;
        int32_t sp = ((int32_t)k * 2 - 5) * p[RSN_SPREAD] / 32;   /* SPREAD: up to ~+-1/4 semitone at the ends */
        if (p[RSN_WANDER]) {                            /* WANDER: each band its own slow walk, up to +-1/2 semitone */
            int32_t aim = (int32_t)(noise32(&s->rng) >> 24) - 128;
            s->bw[k] += ((aim * p[RSN_WANDER] >> 8) - s->bw[k]) >> 6;
        } else {
            s->bw[k] = 0;
        }
        res2_coef(&s->r[k], pitch_inc((uint32_t)clamp(root16 + iv + sp + s->bw[k], 16 * 12, 16 * 126)), t60);
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -262143, 262143) >> 2, xd = (int32_t)(((int64_t)(x - s->x2) * mk) >> 8), y = 0, a;
        s->x2 = s->x1;
        s->x1 = x;
        for (k = 0; k < nb; k++)
            y += res2_run(&s->r[k], xd);
        y = flt_bank_out(y);
        a = y < 0 ? -y : y;
        if (a > pk) pk = a;
        b[i] = mix >= 127 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * mq) >> 15);
    }
    s->ring = pk;
}

/* ------------------------------------------------------------------------------------------- FORMANT --- *
 * Three formants (DESIGN §3.3: distant, choir-like) morphing through A E I O U (VOWEL), SHIFT them +-1 octave (a
 * smaller or larger throat), RES how sharp; KEYTRK SHIFT follows the notes, WANDER the vowel drifts on its own */
enum { FMT_SHIFT, FMT_RES, FMT_VOWEL, FMT_MIX, FMT_KTRK, FMT_WANDER };
static const uint16_t FMT_HZ[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
                                      {300, 870, 2240}};   /* A E I O U (Peterson and Barney's means) */
static const int16_t FMT_GAIN[3] = {32767, 20000, 11000};
static const param_desc_t FLT_PD_FORMANT[8] = {
    {"SHFT", F_BIPCT, -64, 63, 0, 0, 0},
    {"RES", F_PCT, 0, 127, 60, 0, 0},
    {"VOWL", F_PCT, 0, 127, 0, 0, 0},
    {"MIX", F_PCT, 0, 127, 127, 0, 0},
    {"KTRK", F_PCT, 0, 127, 0, 0, 0},
    {"WNDR", F_PCT, 0, 127, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
    {"-", F_INT, 0, 0, 0, 0, 0},
};
#define FLT_T_FORMANT {"FORMANT", 80, FLT_PD_FORMANT}

static void flt_formant(flt_state_t *s, const track_t *t, const int16_t *p, int32_t *b, uint32_t n)
{
    int32_t v = p[FMT_VOWEL] * 4, mix = p[FMT_MIX], mq = mix * 258, pk = 0, seg, fr, sh16;
    uint32_t k, i, t60 = TIME_MS_X10[clamp(20 + p[FMT_RES] * 40 / 127, 0, 127)];
    uint32_t ratio;                                     /* SHIFT (+ KEYTRK), Q16 */
    int32_t mk = res2_makeup(t60, 3u);
    if (!s->on) {
        int32_t r = s->rng ? s->rng : 0x1F2E3D4C;
        memset(s, 0, sizeof *s);
        s->on = 1;
        s->rng = r;
    }
    v += (flt_wander(s, p[FMT_WANDER]) * p[FMT_WANDER] * 4) >> 15;   /* WANDER: the vowel, +-4 vowels' worth x .. */
    v = clamp(v, 0, 508);
    seg = v / 127;
    fr = v - seg * 127;
    if (seg >= 4) { seg = 3; fr = 127; }
    sh16 = p[FMT_SHIFT] * 12 * 16 / 64;
    if (p[FMT_KTRK])
        sh16 += ((int32_t)flt_note(t) - 60) * 16 * p[FMT_KTRK] / 127;
    sh16 = clamp(sh16, -16 * 24, 16 * 24);
    ratio = pitch_inc((uint32_t)(960 + sh16)) / (pitch_inc(960u) >> 16);   /* (32-bit divide: ~4 cents) */
    for (k = 0; k < 3u; k++) {
        uint32_t hz = ((uint32_t)FMT_HZ[seg][k] * (uint32_t)(127 - fr) + (uint32_t)FMT_HZ[seg + 1][k] * (uint32_t)fr) / 127u;
        uint32_t inc = (uint32_t)(((uint64_t)hz * 97392u * ratio) >> 16);   /* (2^32 / FS = 97391.5) */
        res2_coef(&s->r[k], inc, t60);
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -262143, 262143) >> 2, xd = (int32_t)(((int64_t)(x - s->x2) * mk) >> 8), y = 0, a;
        s->x2 = s->x1;
        s->x1 = x;
        for (k = 0; k < 3u; k++)
            y += mulq15(res2_run(&s->r[k], xd), FMT_GAIN[k]);
        y = flt_bank_out(y);
        a = y < 0 ? -y : y;
        if (a > pk) pk = a;
        b[i] = mix >= 127 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * mq) >> 15);
    }
    s->ring = pk;
}

/* the filter still sounds (above the noise): the track's mix runs on after its voices */
static int flt_ringing(const flt_state_t *s)
{
    return s->on && (s->ic1 > 16 || s->ic1 < -16 || s->ic2 > 16 || s->ic2 < -16 || s->ring > 32);
}
