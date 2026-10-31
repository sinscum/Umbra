/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA DRIVE, type DAMAGE (docs/DESIGN.md §3.4): colour and damage, in a fixed order
 *   DRIVE -> FOLD -> CRUSH -> DECIM -> NOISE -> TILT -> COMP -> OUT
 * on the track's block (fx.c mix_part, after SOURCE / GRANULAR / FILTER; the base tree's scale: Q15 a voice, more for
 * chords). Each stage at its default (0; TILT 0, OUT 0 dB) is skipped, so DAMAGE at its defaults is the input bit for
 * bit; the DC blocker runs only behind a stage that makes DC (DRIVE's tanh is odd, but FOLD, CRUSH, DECIM and NOISE
 * are not). Fixed point throughout: tanh from the engines' table (dsp.c softclip), the gains Q12, the coefficients
 * Q15. Knob-driven gains move across a block (no zipper); COMP's gain is set once a block from its envelope (one
 * divide a block, none a sample). Saturates, never wraps. */
enum { DRV_DRIVE, DRV_FOLD, DRV_CRUSH, DRV_DECIM, DRV_NOISE, DRV_TILT, DRV_COMP, DRV_OUT };
#define DRV_OUT_UNITY 112                       /* OUT: LEVEL_Q12 / LEVEL_DB_X10's 0 dB (4096) */

static const param_desc_t DRV_PD_DAMAGE[DEV_NP] = {
    {"DRV", F_PCT, 0, 127, 0, 0, 0},
    {"FOLD", F_PCT, 0, 127, 0, 0, 0},
    {"CRSH", F_PCT, 0, 127, 0, 0, 0},
    {"DCM", F_PCT, 0, 127, 0, 0, 0},
    {"NOIS", F_PCT, 0, 127, 0, 0, 0},
    {"TILT", F_BIPCT, -64, 63, 0, 0, 0},
    {"COMP", F_PCT, 0, 127, 0, 0, 0},
    {"OUT", F_DB, 0, 127, DRV_OUT_UNITY, 0, 0},       /* OFF .. +7.5 dB, 0 dB by default */
};
/* cost: host instructions per sample at full settings (tests/drive_test.c measures it; the type's line there) */
#define DRV_T_DAMAGE {"DAMAGE", 140, DRV_PD_DAMAGE}

typedef struct {
    uint8_t on;                 /* ran last block (else: a fresh start, no ramp from stale values) */
    int32_t g;                  /* DRIVE's gain, Q12, as the last sample had it */
    int32_t out;                /* OUT's gain, Q12 */
    int32_t cg;                 /* COMP's gain, Q12 */
    int32_t env;                /* COMP's envelope */
    int32_t tilt_lp;            /* TILT's low band */
    int32_t hold, hacc;         /* DECIM: the held sample, the phase (Q8 samples) */
    int32_t nz, nz_lp;          /* NOISE: its generator, its colour */
    int32_t dc_x, dc_y;         /* the DC blocker */
} drv_state_t;
static drv_state_t drv_st[NTRK];

#define DRV_LIM 262143          /* the block's values stay within +-2^18 (8 loud voices) */

static inline int32_t drv_gain(int32_t v) { return 4096 + v * v * 2; }        /* DRIVE 0..127: Q12 1x .. ~9x */
static inline int32_t drv_fold_gain(int32_t v) { return 4096 + v * 96; }      /* FOLD 0..127: Q12 1x .. ~4x */
static inline int32_t drv_crush_sh(int32_t v) { return v * 12 / 127; }       /* CRUSH 0..127: 0 .. 12 bits gone */

/* the shaping stages, sample by sample (the audio and the page's curve: ui_graph.c graph_drive): DRIVE's tanh at gain
 * g (on when drive), FOLD at gain fg (on when fold), CRUSH's sh bits */
static inline int32_t drv_shape(int32_t x, int32_t drive, int32_t g, int32_t fold, int32_t fg, int32_t sh)
{
    if (drive)                                          /* tanh: x * g into the table (Q15 in, Q15 out) */
        x = softclip(clamp((int32_t)(((int64_t)x * g) >> 12), -65535, 65535));
    if (fold) {                                         /* triangle fold back into +-32767 (bounded: <= 9 reflections) */
        int32_t u = clamp((int32_t)(((int64_t)x * fg) >> 12), -294903, 294903), k;
        for (k = 0; k < 9 && (u > 32767 || u < -32767); k++)
            u = u > 32767 ? 65534 - u : -65534 - u;
        x = u;
    }
    if (sh)
        x &= ~((1 << sh) - 1);
    return x;
}

static void drv_damage(drv_state_t *s, const int16_t *p, int32_t *b, uint32_t n)
{
    int32_t drive = p[DRV_DRIVE], fold = p[DRV_FOLD], crush = p[DRV_CRUSH], decim = p[DRV_DECIM];
    int32_t noise = p[DRV_NOISE], tilt = p[DRV_TILT], comp = p[DRV_COMP], outv = p[DRV_OUT];
    int32_t gt = drv_gain(drive), ot = LEVEL_Q12[outv & 127], g, gs, o, os;
    int32_t fg = drv_fold_gain(fold), sh = drv_crush_sh(crush);
    int32_t per = 256 + decim * decim * 2;              /* DECIM: hold period, Q8 samples: 1 .. ~127 (347 Hz) */
    int32_t nl = (noise * noise) >> 3;                  /* NOISE: its level, 0 .. ~2000 (-24 dBFS) */
    int32_t tq = tilt * 512;                            /* TILT: Q15 -1 .. +1 */
    int32_t dcy = drive | fold | crush | decim | noise; /* (a stage that makes DC: the blocker) */
    uint32_t i;
    if (!s->on) {                                       /* a fresh start: no ramp from what it was */
        memset(s, 0, sizeof *s);
        s->on = 1;
        s->g = gt;
        s->out = ot;
        s->cg = 4096;
        s->nz = 0x2545F491;
    }
    g = s->g;
    o = s->out;
    gs = (gt - g) / (int32_t)n;                         /* the gains cross the block (no zipper) */
    os = (ot - o) / (int32_t)n;
    if (comp) {                                         /* COMP: the block's gain from the envelope (one divide) */
        int32_t th = 32767 - comp * 200, want = 4096;   /* threshold -0 .. -13 dBFS, ratio ~1:1 .. 4:1 */
        if (s->env > th) {
            int32_t over = s->env - th, ratio = 1 + comp / 40;
            want = ((th + over / ratio) << 12) / s->env;   /* (< 2^27 / >= 7367: 32 bits, no 64-bit divide) */
        }
        want = (want * (4096 + comp * 12)) >> 12;       /* make-up */
        s->cg += (want - s->cg) >> 2;
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -DRV_LIM, DRV_LIM);
        g += gs;
        o += os;
        x = drv_shape(x, drive, g, fold, fg, sh);
        if (decim) {                                    /* sample and hold */
            s->hacc += 256;
            if (s->hacc >= per) {
                s->hacc -= per;
                s->hold = x;
            }
            x = s->hold;
        }
        if (nl) {                                       /* hiss, darkened, and now and then a crackle */
            uint32_t r = noise32(&s->nz);
            int32_t w = ((int32_t)r >> 16) + ((r & 0x3FFu) == 0u ? ((int32_t)r >> 2 & 0x7FFF) - 16384 : 0);
            s->nz_lp += (w - s->nz_lp) >> 2;
            x += (s->nz_lp * nl) >> 14;
        }
        if (tq) {                                       /* tilt about ~800 Hz: + brighter, - darker */
            s->tilt_lp += ((x - s->tilt_lp) * 3510 + 16384) >> 15;
            x += (int32_t)(((int64_t)(x - 2 * s->tilt_lp) * tq) >> 15);
        }
        if (comp) {
            int32_t a = x < 0 ? -x : x;
            s->env += a > s->env ? (a - s->env) >> 4 : -((s->env - a) >> 11);   /* ~0.4 ms attack, ~46 ms release */
            x = (int32_t)(((int64_t)x * s->cg) >> 12);
        }
        if (o != 4096)
            x = (int32_t)(((int64_t)x * o) >> 12);
        if (dcy) {                                      /* DC blocker, ~5 Hz (R = 1 - 2 pi 5 / FS) */
            int32_t y = x - s->dc_x + ((s->dc_y * 32745 + 16384) >> 15);   /* (rounded: a floor in this loop
                                                                              * would add -0.5 x 1400 of DC) */
            s->dc_x = x;
            s->dc_y = y;
            x = y;
        }
        b[i] = clamp(x, -DRV_LIM, DRV_LIM);
    }
    s->g = gt;
    s->out = ot;
}
