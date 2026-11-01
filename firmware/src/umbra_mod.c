/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA MODS (docs/DESIGN.md §5, Phase 5a, kept simple): two per track, each moving one chain value up and down.
 * Pages MOD 1 / MOD 2 (the end of LFO's family, after the base tree's MOD matrix): SHAPE RATE DEST DEPTH.
 *   SHAPE  SINE TRI SAW SQR S&H (a new random value each cycle) DRIFT (a new random value each cycle, glided to)
 *   RATE   a cycle 10 min .. 0.1 s (exponential; 64: ~7.5 s)
 *   DEST   OFF or a value of the track's SOURCE, GRANULAR, FILTER or DRIVE as its type has it (a name: never an
 *          F_ENUM choice such as MODE or REC, which would jump); stored as device x 12 + value + 1. A type change
 *          leaves it pointing at a value the new type may lack: then it does nothing until DEST is turned
 *   DEPTH  -64..63: at 63 the value swings over its whole range (clamped at its ends); negative: upside down
 * Per block (control rate, ~0.7 ms), in the audio ISR as the base tree's matrix (mod.c): umod_begin writes the
 * moved values into the chain for the track's block, umod_end puts the stored ones back, so the UI, projects and the
 * editor only ever see the stored values. A slot with DEST OFF or DEPTH 0 costs nothing; nothing set: bit-identical.
 * The phase runs on whatever plays (free-running, not reset by notes). UMBRA_MOD 0: the pages stay, nothing moves. */
#ifndef UMBRA_MOD
#define UMBRA_MOD UMBRA_CHAIN
#endif
#define UMOD_N 2u                                       /* MODS a track */
#define UMOD_NP 4u                                      /* values a MOD */
enum { UM_SHAPE, UM_RATE, UM_DEST, UM_DEPTH };
enum { UMS_SINE, UMS_TRI, UMS_SAW, UMS_SQR, UMS_SH, UMS_DRIFT, UMS_N };
static const char *const UMS_NM[UMS_N] = {"SINE", "TRI", "SAW", "SQR", "S&H", "DRIFT"};
#define UMOD_DEST_N (DEV_COUNT * DEV_NP + 1)            /* OFF + every device's values */
static const char *umod_dest_nm[UMOD_DEST_N];           /* (F_ENUM's names: umod_format draws DEST itself) */
static const param_desc_t UMOD_PD[UMOD_NP] = {
    {"SHAP", F_ENUM, 0, UMS_N - 1, UMS_SINE, UMS_NM, 0},
    {"RATE", F_INT, 0, 127, 64, 0, 0},                  /* (umod_format: its cycle) */
    {"DEST", F_ENUM, 0, UMOD_DEST_N - 1, 0, umod_dest_nm, 0},
    {"DPTH", F_BIPCT, -64, 63, 32, 0, 0},
};
/* a cycle in centiseconds by RATE: 60000 x 6000^(-r / 127), 10 min .. 0.1 s */
static const uint16_t UMOD_CS[128] = {
    60000, 56028, 52318, 48854, 45620, 42600, 39779, 37146, 34686, 32390,
    30245, 28243, 26373, 24627, 22997, 21474, 20052, 18725, 17485, 16327,
    15246, 14237, 13294, 12414, 11592, 10825, 10108, 9439, 8814, 8230,
    7686, 7177, 6702, 6258, 5844, 5457, 5095, 4758, 4443, 4149,
    3874, 3618, 3378, 3155, 2946, 2751, 2569, 2398, 2240, 2091,
    1953, 1824, 1703, 1590, 1485, 1387, 1295, 1209, 1129, 1054,
    984, 919, 858, 802, 749, 699, 653, 609, 569, 531,
    496, 463, 433, 404, 377, 352, 329, 307, 287, 268,
    250, 234, 218, 204, 190, 178, 166, 155, 145, 135,
    126, 118, 110, 103, 96, 90, 84, 78, 73, 68,
    64, 59, 55, 52, 48, 45, 42, 39, 37, 34,
    32, 30, 28, 26, 24, 23, 21, 20, 19, 17,
    16, 15, 14, 13, 12, 11, 11, 10,
};
#define UMOD_INC_K 311652955u                           /* 2^32 x 100 x CTL / FS: a block's phase step x centiseconds */
_Static_assert(CTL == 32 && FS == 44100, "UMOD_INC_K is for 44.1 kHz blocks of 32");

static volatile int16_t umod[NTRK][UMOD_N][UMOD_NP];   /* the values (the UI writes, the audio reads) */
typedef struct {
    uint32_t ph;                                        /* the cycle, Q32 */
    uint32_t rng;
    int32_t from, to;                                   /* S&H, DRIFT: the value before, the one now (Q15) */
    int16_t keep;                                       /* umod_begin -> umod_end: the stored value */
    uint8_t kd, kk;                                     /* .. its device and index + 1 (0: none moved) */
} umod_st_t;
static umod_st_t umod_st[NTRK][UMOD_N];                    /* (the audio's own) */

static void umod_reset(void)                            /* (chain_reset: every MOD its defaults, DEST OFF) */
{
    uint32_t tr, s, k;
    for (tr = 0; tr < NTRK; tr++)
        for (s = 0; s < UMOD_N; s++) {
            for (k = 0; k < UMOD_NP; k++)
                umod[tr][s][k] = UMOD_PD[k].def;
            umod_st[tr][s].rng = 0x9E3779B9u * (tr * UMOD_N + s + 1u);
        }
}

/* DEST v on track tr: the device's value it names (pd[k]), 0 if none there now (OFF, no such value, a choice) */
static const param_desc_t *umod_target(uint32_t tr, int32_t v, uint32_t *d, uint32_t *k)
{
    const param_desc_t *pd;
    if (v <= 0 || v >= UMOD_DEST_N)
        return 0;
    *d = (uint32_t)(v - 1) / DEV_NP;
    *k = (uint32_t)(v - 1) % DEV_NP;
    pd = dev_type(tr, *d)->pd;
    if (!pd || !pd[*k].label || !pd[*k].label[0] || pd[*k].fmt == F_ENUM || pd[*k].max <= pd[*k].min)
        return 0;
    return &pd[*k];
}

/* params.c param_format: RATE as its cycle, DEST as the value's name and its device. 1 = done */
static int umod_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (d == &UMOD_PD[UM_RATE]) {
        uint32_t cs = UMOD_CS[v & 127];
        if (cs >= 6000u) {                              /* minutes */
            fmt_fix(val, (int32_t)(cs / 600u), 1);
            *unit = "m";
        } else if (cs >= 1000u) {
            fmt_int(val, (int32_t)((cs + 50u) / 100u));
            *unit = "s";
        } else {                                        /* (fmt_fix: ".5" for 5; "0.5") */
            val[0] = '0';
            fmt_fix(cs < 100u ? val + 1 : val, (int32_t)(cs / 10u), 1);
            *unit = "s";
        }
        return 1;
    }
    if (d == &UMOD_PD[UM_DEST]) {
        uint32_t dv, k;
        const param_desc_t *t = umod_target(song.sel % NTRK, v, &dv, &k);
        static const char *const TAG[DEV_COUNT] = {"S", "G", "F", "D", "SP"};   /* (the device: its first letter) */
        str_cpy(val, t ? t->label : "OFF", 6);
        *unit = t ? TAG[dv] : "";
        return 1;
    }
    return 0;
}
/* params.c enum_skip: DEST passes over what the selected track's devices lack now */
static int umod_skip(const param_desc_t *d, int32_t v)
{
    uint32_t dv, k;
    return d == &UMOD_PD[UM_DEST] && v != 0 && !umod_target(song.sel % NTRK, v, &dv, &k);
}
/* params.c page_desc, scope SC_UMOD: id = MOD x 4 + value, of the selected track */
static const param_desc_t *umod_desc(uint32_t id, int16_t **valp)
{
    if (id >= UMOD_N * UMOD_NP) {
        *valp = 0;
        return 0;
    }
    *valp = (int16_t *)&umod[song.sel % NTRK][id / UMOD_NP][id % UMOD_NP];
    return &UMOD_PD[id % UMOD_NP];
}

static uint32_t umod_rand(uint32_t *r)                  /* xorshift32 */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}
/* the shape at phase ph, Q15 bipolar (-32767 .. 32767) */
static int32_t umod_wave(umod_st_t *m, uint32_t shape, uint32_t ph)
{
    switch (shape) {
    case UMS_TRI: {
        int32_t x = (int32_t)(ph >> 16);                /* 0 .. 65535 */
        return x < 32768 ? (x * 2 - 32767) : (32767 - (x - 32768) * 2);
    }
    case UMS_SAW:
        return (int32_t)(ph >> 16) - 32768;
    case UMS_SQR:
        return ph < 0x80000000u ? 32767 : -32767;
    case UMS_SH:
        return m->to;
    case UMS_DRIFT:                                     /* from -> to over the cycle */
        return m->from + (int32_t)(((int64_t)(m->to - m->from) * (int32_t)(ph >> 17)) >> 15);
    default:
        return SINE[ph >> 22];                          /* (SINE: 1024 points a cycle) */
    }
}

/* fx.c mix_part, a track's block: its MODS move their values (the stored ones kept for umod_end) */
static __attribute__((noinline)) void umod_begin(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, s;
    for (s = 0; s < UMOD_N; s++) {
        umod_st_t *m = &umod_st[tr][s];
        const volatile int16_t *u = umod[tr][s];
        const param_desc_t *pd;
        uint32_t d, k, old = m->ph;
        int32_t w, span, v;
        m->kk = 0;
        if (!UMBRA_MOD || !u[UM_DEPTH] || !(pd = umod_target(tr, u[UM_DEST], &d, &k)))
            continue;
        m->ph += UMOD_INC_K / UMOD_CS[u[UM_RATE] & 127];   /* (a 32-bit divide a block: UMOD_CS >= 10) */
        if (m->ph < old) {                              /* a new cycle: S&H and DRIFT's next value */
            m->from = m->to;
            m->to = (int32_t)(umod_rand(&m->rng) >> 17) - 16384;
            m->to *= 2;
        }
        w = umod_wave(m, (uint32_t)u[UM_SHAPE] % UMS_N, m->ph);
        span = pd->max - pd->min;
        m->keep = uchain[tr].p[d][k];
        m->kd = (uint8_t)d;
        m->kk = (uint8_t)(k + 1u);
        v = m->keep + (int32_t)(((int64_t)w * u[UM_DEPTH] * span) >> 21);   /* (Q15 x 64: the whole range at 63) */
        uchain[tr].p[d][k] = (int16_t)clamp(v, pd->min, pd->max);
    }
}
/* .. and back, the latest first (two MODS on one value: the stored one again) */
static __attribute__((noinline)) void umod_end(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, s;
    for (s = UMOD_N; s-- > 0;) {
        umod_st_t *m = &umod_st[tr][s];
        if (m->kk) {
            uchain[tr].p[m->kd][m->kk - 1u] = m->keep;
            m->kk = 0;
        }
    }
}
