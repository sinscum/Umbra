/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA SOURCE TAPE (docs/DESIGN.md §3.1): a loop recorder. The tracks set to TAPE share UMBRA_TAPE_BYTES (38 KiB,
 * umbra_mem.h), in equal parts in track order (one TAPE track: all of it); a part changing (a track to or from TAPE)
 * empties them. At 22.05 kHz (a value a pair of samples, the pair's average): HI 16-bit (0.88 s for one track), LO 8-bit
 * mu-law (1.76 s), changing the quality empties it.
 *   FROM  MIX (the mix of the block before, before the limiter), T1..T4 (a track after its chain, the block before:
 *         umbra_source.c), SELF (the track's own engine: monitored while recording; over a take, an overdub with FDBK)
 *   REC   ON: a new take from the start (SELF over a take: overdubs where it plays) until OFF or full (then it plays,
 *         REC still on); CLEAR asks first
 *         (ui_input.c), then the take and the track's GRANULAR ring are emptied, a chunk a block (no CPU spike)
 *   MODE  LOOP: plays START .. START + LENGTH of the take round and round at PITCH (varispeed), DIR forward or reversed,
 *         its ends faded (no click); KEYS: the track's notes play it chromatically from START (C4 = PITCH), 4 voices,
 *         ATK / REL, once to the segment's end
 * The track's engine still takes its notes (KEYS uses them; SYNTH again is instant). GRANULAR on a TAPE track in LO reads
 * the take itself (umbra_granular.c), in HI its own ring (of the TAPE's sound). Never saved (RAM only). */
#ifndef UMBRA_TAPE
#define UMBRA_TAPE UMBRA_CHAIN                          /* 0: TAPE stays a type (stored projects keep it) but is silent */
#endif
enum { TP_LVL, TP_PITCH, TP_START, TP_LEN, TP_ATK, TP_REL, TP_MODE, TP_DIR, TP_FROM, TP_QUAL, TP_REC, TP_FDBK };
enum { TMODE_LOOP, TMODE_KEYS, TMODE_N };
enum { TDIR_FWD, TDIR_REV, TDIR_N };
enum { TFROM_MIX, TFROM_T1, TFROM_T2, TFROM_T3, TFROM_T4, TFROM_SELF, TFROM_N };
enum { TQ_HI, TQ_LO, TQ_N };
enum { TREC_OFF, TREC_ON, TREC_CLEAR, TREC_N };
static const char *const TMODE_NM[TMODE_N] = {"LOOP", "KEYS"};
static const char *const TDIR_NM[TDIR_N] = {"FWD", "REV"};
static const char *const TFROM_NM[TFROM_N] = {"MIX", "T1", "T2", "T3", "T4", "SELF"};
static const char *const TQ_NM[TQ_N] = {"HI", "LO"};
static const char *const TREC_NM[TREC_N] = {"OFF", "REC", "CLR"};
static const param_desc_t SRC_PD_TAPE[DEV_NP] = {
    {"LVL", F_DB, 0, 127, 112, 0, 0},
    {"PTCH", F_SEMI, -24, 24, 0, 0, 0},
    {"STRT", F_PCT, 0, 127, 0, 0, 0},
    {"LEN", F_PCT, 1, 127, 127, 0, 0},
    {"ATK", F_PCT, 0, 127, 4, 0, 0},
    {"REL", F_PCT, 0, 127, 30, 0, 0},
    {"MODE", F_ENUM, 0, TMODE_N - 1, TMODE_LOOP, TMODE_NM, 0},
    {"DIR", F_ENUM, 0, TDIR_N - 1, TDIR_FWD, TDIR_NM, 0},
    {"FROM", F_ENUM, 0, TFROM_N - 1, TFROM_MIX, TFROM_NM, 0},
    {"QUAL", F_ENUM, 0, TQ_N - 1, TQ_LO, TQ_NM, 0},
    {"REC", F_ENUM, 0, TREC_N - 1, TREC_OFF, TREC_NM, 0},
    {"FDBK", F_PCT, 0, 127, 100, 0, 0},
};

#define TAPE_VOICES 4u
#define TAPE_FADE 64u                                   /* the loop's ends: values faded (2.9 ms) */
#define TAPE_CLR_CHUNK 2048u                            /* CLEAR: bytes a block */
static uint8_t tape_mem[UMBRA_TAPE_BYTES] __attribute__((section(".pool")));
typedef struct {
    uint8_t on, note, gate;
    uint32_t pos;                                       /* Q16 values */
    int32_t inc, env;                                   /* Q16 a sample; Q15 */
} tvoice_t;
typedef struct {
    uint8_t q, rec, od, full;                           /* quality held, recording, overdubbing, the take full */
    uint32_t base, size;                                /* its part: bytes */
    uint32_t len, w;                                    /* the take's values, the write */
    int32_t acc;
    uint32_t odd;
    uint32_t pos;                                       /* LOOP: Q16 values */
    uint32_t clr;                                       /* CLEAR: bytes left to empty */
    tvoice_t v[TAPE_VOICES];
} tape_t;
static tape_t tps[NTRK];
static uint8_t tape_part;                               /* the tracks the parts were made for */
static int32_t tape_mix[CTL];                           /* the mix of the block before (FROM MIX) */
static volatile uint8_t tape_clr_ui[NTRK];             /* CLEAR confirmed: the UI counts up (its writer) */
static uint8_t tape_clr_seen[NTRK];                     /* .. the audio's count of them (its writer) */

static inline uint32_t tape_cap(const tape_t *s) { return s->q == TQ_HI ? s->size / 2u : s->size; }
static inline int32_t tape_get(const tape_t *s, uint32_t k)
{
    if (s->q == TQ_HI)
        return ((const int16_t *)(const void *)(tape_mem + s->base))[k];
    return GRN_ULAW[tape_mem[s->base + k]];
}
static inline void tape_put(const tape_t *s, uint32_t k, int32_t x)
{
    x = clamp(x, -32767, 32767);
    if (s->q == TQ_HI)
        ((int16_t *)(void *)(tape_mem + s->base))[k] = (int16_t)x;
    else
        tape_mem[s->base + k] = ulaw_enc(x);
}

/* the start of a block (fx.c mix_block): the parts for the tracks set to TAPE now, CLEAR's chunks */
static __attribute__((noinline)) void tape_block(uint8_t mask)
{
    uint32_t tr, k = 0, n = (uint32_t)__builtin_popcount(mask);
    if (mask != tape_part) {                            /* (a track to or from TAPE: new parts, empty) */
        for (tr = 0; tr < NTRK; tr++) {
            tape_t *s = &tps[tr];
            uint8_t q = s->q;
            memset(s, 0, sizeof *s);
            s->q = q;
            if ((mask >> tr) & 1u) {
                s->size = (UMBRA_TAPE_BYTES / n) & ~3u;
                s->base = k++ * s->size;
            }
        }
        tape_part = mask;
    }
    for (tr = 0; tr < NTRK; tr++) {
        tape_t *s = &tps[tr];
        if (tape_clr_seen[tr] != tape_clr_ui[tr]) {     /* CLEAR: the take, its voices, the track's GRANULAR ring */
            uint32_t g;
            tape_clr_seen[tr] = tape_clr_ui[tr];
            s->len = s->w = 0;
            s->full = s->od = 0;
            s->clr = s->size;
            memset(s->v, 0, sizeof s->v);
            grs[tr].filled = 0;                         /* (the ring's valid part: none; no grain reads it) */
            for (g = 0; g < GRN_MAX; g++)
                if (grn[g].tr == tr)
                    grn[g].on = 0;
        }
        if (s->clr) {                                   /* (a chunk a block) */
            uint32_t c = s->clr > TAPE_CLR_CHUNK ? TAPE_CLR_CHUNK : s->clr;
            memset(tape_mem + s->base + s->size - s->clr, s->q == TQ_HI ? 0 : 0xFF, c);   /* (mu-law 0xFF: 0) */
            s->clr -= c;
        }
    }
}
/* the end of the mix (fx.c mix_block): the block for FROM MIX next time */
static __attribute__((noinline)) void tape_take_mix(const int32_t *l, const int32_t *r, const int32_t *wet, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < CTL; i++)
        tape_mix[i] = (l[i] + r[i] + 2 * wet[i]) >> 2;  /* (the master's level at MASTER 2048, mono) */
}

/* a note on track tr in KEYS (umbra_chain.c tape_note): its voices (a free one, else the quietest) */
static void tape_keys(uint32_t tr, uint32_t note, int on)
{
    tape_t *s = &tps[tr % NTRK];
    uint32_t k, best = 0;
    if (on) {
        for (k = 0; k < TAPE_VOICES; k++) {
            if (!s->v[k].on) {
                best = k;
                break;
            }
            if (s->v[k].env < s->v[best].env)
                best = k;
        }
        s->v[best].note = (uint8_t)note;
        s->v[best].gate = 1;
        s->v[best].on = 2;                              /* (2: to start in the audio) */
    } else {
        for (k = 0; k < TAPE_VOICES; k++)
            if (s->v[k].on && s->v[k].note == note)
                s->v[k].gate = 0;
    }
}

/* the track's block b (its engine's, SELF) -> the tape's: recording, then LOOP or KEYS at LEVEL */
static __attribute__((noinline)) void tape_run(uint32_t tr, const int16_t *p, int32_t *b, uint32_t n)
{
    tape_t *s = &tps[tr];
    uint32_t i, k, cap, s0, seg, from = (uint32_t)p[TP_FROM];
    int32_t g = LEVEL_Q12[p[TP_LVL] & 127], rec = p[TP_REC] == TREC_ON, inc;
    static int32_t in[CTL], out[CTL];
    if (!s->size) {                                     /* (no part: tape_block has not run yet) */
        for (i = 0; i < n; i++)
            b[i] = 0;
        return;
    }
    if (s->q != (uint8_t)p[TP_QUAL]) {                  /* (another quality: the take means nothing now) */
        s->q = (uint8_t)p[TP_QUAL];
        s->len = s->w = 0;
        s->full = 0;
    }
    cap = tape_cap(s);
    for (i = 0; i < n; i++)
        in[i] = from == TFROM_SELF ? b[i] : from == TFROM_MIX ? tape_mix[i % CTL] : src_ret[1][(from - TFROM_T1) % NTRK][i % CTL];
    if (rec && !s->rec) {                               /* REC on: a new take, or (SELF over a take) an overdub */
        s->od = from == TFROM_SELF && s->len >= TAPE_FADE * 2u;
        if (!s->od) {
            s->len = s->w = 0;
            s->full = 0;
            s->odd = 0;
            s->pos = 0;
        }
    }
    s->rec = (uint8_t)rec;
    if (rec && !s->od && !s->full) {                    /* a new take: a value a pair */
        for (i = 0; i < n; i++) {
            int32_t x = clamp(in[i], -65535, 65535) >> 1;
            if (!s->odd) {
                s->acc = x;
            } else {
                tape_put(s, s->w, (s->acc + x) >> 1);
                if (++s->w >= cap) {
                    s->full = 1;
                    s->len = cap;
                    break;
                }
                s->len = s->w;
            }
            s->odd ^= 1u;
        }
    }
    for (i = 0; i < n; i++)
        out[i] = 0;
    s0 = s->len * (uint32_t)p[TP_START] / 128u;
    seg = s->len > s0 ? (s->len - s0) * (uint32_t)p[TP_LEN] / 127u : 0u;
    if (seg >= TAPE_FADE * 2u && !(rec && !s->od && !s->full)) {   /* (not while a new take is going; full: it plays) */
        int32_t ratio = grn_ratio_q16(p[TP_PITCH]) >> 1;   /* (values a sample: the take a value a pair) */
        uint32_t end = s0 + seg;
        if (p[TP_MODE] == TMODE_LOOP) {
            inc = p[TP_DIR] == TDIR_REV ? -ratio : ratio;
            if ((s->pos >> 16) < s0 || (s->pos >> 16) >= end)
                s->pos = (p[TP_DIR] == TDIR_REV ? end - 1u : s0) << 16;
            for (i = 0; i < n; i++) {
                uint32_t ri = s->pos >> 16, rj = ri + 1u >= end ? s0 : ri + 1u, e0 = ri - s0, e1 = end - 1u - ri;
                int32_t a = tape_get(s, ri), c = tape_get(s, rj), x = a + (((c - a) * (int32_t)((s->pos >> 8) & 255u)) >> 8);
                uint32_t edge = e0 < e1 ? e0 : e1;
                if (edge < TAPE_FADE)                   /* (the loop's ends faded) */
                    x = x * (int32_t)edge / (int32_t)TAPE_FADE;
                out[i] = x;
                if (s->od && rec) {                     /* SELF overdub: in + what was there x FDBK, here */
                    int32_t o = (tape_get(s, ri) * p[TP_FDBK]) / 127;
                    tape_put(s, ri, o + (clamp(in[i], -65535, 65535) >> 1));
                }
                s->pos = (uint32_t)((int32_t)s->pos + inc);
                if ((s->pos >> 16) >= end || (int32_t)s->pos < (int32_t)(s0 << 16))
                    s->pos = (inc < 0 ? end - 1u : s0) << 16;
            }
        } else {                                        /* KEYS: the voices from START, once, ATK / REL */
            int32_t up = 32767 / (1 + p[TP_ATK] * p[TP_ATK] / 2), dn = 32767 / (1 + p[TP_REL] * p[TP_REL]);
            for (k = 0; k < TAPE_VOICES; k++) {
                tvoice_t *v = &s->v[k];
                if (!v->on)
                    continue;
                if (v->on == 2u) {
                    v->on = 1;
                    v->pos = s0 << 16;
                    v->env = 0;
                    v->inc = grn_ratio_q16(clamp((int32_t)v->note - 60 + p[TP_PITCH], -36, 36)) >> 1;
                }
                for (i = 0; i < n; i++) {
                    uint32_t ri = v->pos >> 16;
                    int32_t a, c, x;
                    if (ri + 1u >= end) {
                        v->on = 0;
                        break;
                    }
                    a = tape_get(s, ri);
                    c = tape_get(s, ri + 1u);
                    x = a + (((c - a) * (int32_t)((v->pos >> 8) & 255u)) >> 8);
                    v->env = v->gate ? (v->env + up > 32767 ? 32767 : v->env + up) : v->env - dn;
                    if (v->env <= 0 && !v->gate) {
                        v->on = 0;
                        break;
                    }
                    out[i] += (x * v->env) >> 15;
                    v->pos += (uint32_t)v->inc;
                }
            }
        }
    }
    for (i = 0; i < n; i++) {                           /* (x2: the take held half), LEVEL; SELF recording: monitored */
        int32_t y = (int32_t)(((int64_t)out[i] * 2 * g) >> 12);
        b[i] = rec && from == TFROM_SELF ? y + b[i] : y;
    }
}
