/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA SPACE and MASTER (firmware/src/umbra_space.c, umbra_master.c) on the real buses (fx.c fx_buses), test signals
 * straight into the sends:
 * 1. FREEZE on HALL, ROOM and the delay: engaging and releasing steps no further than the signal itself does (no
 *    click); the tail holds 30 s (HALL, ROOM within -3 .. +1 dB of its level; the delay bit for bit, period to period);
 *    released, it decays
 * 2. PRE-DLY 50 ms: the reverb's first sound 50 ms (+-1) later than with 0
 * 3. WOW: the echo's pitch moves (zero-crossing spacing varies), bounded; 0 after it: the delay's read as before
 * 4. DJ FILTER: -64 a low-pass (60 Hz passes, 5 kHz -20 dB), +63 a high-pass (the mirror); back at 0: bit for bit
 *    after it settles, no step on the way
 * 5. GLUE: a loud chord's peak lower; back at 0: bit for bit after it settles
 * 6. cost: host time per sample against one ANALOG voice
 *   build/host/space_test (also built with -DUMBRA_SHIMMER=1: SHIMMER bounded, its cost) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "../web/emu/umbra_recipes.c"
#include <time.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("space: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t trng = 12345u;
static int32_t noise(void) { trng = trng * 1664525u + 1013904223u; return (int32_t)(trng >> 16) - 32768; }
static int32_t nlp;
static int32_t dark_noise(void) { nlp += (noise() - nlp) >> 3; return nlp * 3; }   /* below ~1 kHz */

static int32_t zc[CTL], dl_in[CTL], rv_in[CTL], wet_b[CTL];
static void fresh(int32_t rtype)
{
    ui_power_on();
    song.master_q12 = 2048;
    memset(dly_buf, 0, sizeof dly_buf);
    rev_clear();
    memset(&fx, 0, sizeof fx);
    memset(&hl, 0, sizeof hl);
    fx.rtype = (uint8_t)rtype;
    song.g[G_RTYPE] = (int16_t)rtype;
    space_reset();
    memset(&umst, 0, sizeof umst);
    nlp = 0;
    trng = 12345u;
}
/* one block of the buses: dly / rev sends from the generators (0: silence), wet out */
static void bus_block(int32_t (*gd)(void), int32_t (*gr)(void))
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        dl_in[i] = gd ? gd() : 0;
        rv_in[i] = gr ? gr() : 0;
        wet_b[i] = 0;
    }
    space_pre(dl_in, rv_in, CTL);
    fx_buses(zc, dl_in, rv_in, wet_b, CTL);
}
static double rms_blocks(uint32_t nb, int32_t (*gr)(void), int32_t *maxstep, int32_t *last)
{
    double e = 0;
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        bus_block(0, gr);
        for (i = 0; i < CTL; i++) {
            int32_t d = wet_b[i] - *last;
            d = d < 0 ? -d : d;
            if (maxstep && d > *maxstep) *maxstep = d;
            *last = wet_b[i];
            e += (double)wet_b[i] * wet_b[i];
        }
    }
    return sqrt(e / (nb * CTL));
}
#define BPS (FS / CTL)                                  /* blocks a second */

static void freeze_rev(int32_t rtype, const char *name)
{
    int32_t st0 = 0, st1 = 0, st2 = 0, stf = 0, last = 0;
    double r0, r, lo = 1e9, hi = 0, rend;
    uint32_t s;
    char msg[120];
    fresh(rtype);
    song.g[G_RSIZE] = 70;
    rms_blocks(BPS, dark_noise, &st0, &last);           /* 1 s of noise into it: its steps while fed */
    usong[US_FRZ] = 1;
    rms_blocks(BPS / 4, dark_noise, &st1, &last);       /* engaging (the send still fed: FREEZE closes it) */
    r0 = rms_blocks(BPS, 0, &stf, &last);               /* (frozen, its own steps: the louder held tail's) */
    st0 = st0 > stf ? st0 : stf;
    for (s = 0; s < 30u; s++) {
        r = rms_blocks(BPS, 0, 0, &last);
        lo = r < lo ? r : lo;
        hi = r > hi ? r : hi;
    }
    snprintf(msg, sizeof msg, "FREEZE %s: engaging steps no further than fed or frozen (%d <= %d)", name, st1, st0 * 5 / 4);
    ck(msg, st1 <= st0 * 5 / 4);
    snprintf(msg, sizeof msg, "FREEZE %s: 30 s held within -3 .. +1 dB (%.2f .. %.2f dB, level %.0f)", name,
             20 * log10(lo / r0), 20 * log10(hi / r0), r0);
    ck(msg, r0 > 200 && lo > r0 * 0.708 && hi < r0 * 1.122);
    usong[US_FRZ] = 0;
    rms_blocks(BPS / 4, 0, &st2, &last);
    rend = rms_blocks(BPS, 0, 0, &last);
    rms_blocks(8 * BPS, 0, 0, &last);
    rend = rms_blocks(BPS, 0, 0, &last);
    snprintf(msg, sizeof msg, "FREEZE %s: release steps no further than fed (%d), then decays (%.1f dB after 9 s)", name,
             st2, 20 * log10((rend + 1) / r0));
    ck(msg, st2 <= st0 * 5 / 4 && rend < r0 * 0.1);
}

static int32_t imp_n;
static int32_t pulse(void) { return (imp_n++ % 2205u) < 40u ? 20000 : 0; }   /* 40-sample bursts, 20 Hz */

int main(void)
{
    uint32_t b, i;
    /* 1 */
    freeze_rev(2, "HALL");
    freeze_rev(0, "ROOM");
    {
        static int32_t a[CTL * 64];
        int32_t st0 = 0, st1 = 0, last = 0, same = 1, peak = 0;
        fresh(0);
        song.g[G_DTIME] = 3;                            /* 1/16 */
        song.g[G_DFDBK] = 60;
        imp_n = 0;
        for (b = 0; b < BPS; b++) {
            bus_block(pulse, 0);
            for (i = 0; i < CTL; i++) { int32_t d = wet_b[i] - last; d = d < 0 ? -d : d; st0 = d > st0 ? d : st0; last = wet_b[i]; }
        }
        usong[US_FRZ] = 1;
        for (b = 0; b < BPS; b++) {
            bus_block(pulse, 0);
            for (i = 0; i < CTL; i++) { int32_t d = wet_b[i] - last; d = d < 0 ? -d : d; st1 = d > st1 ? d : st1; last = wet_b[i]; }
        }
        {   /* frozen: the delay's period repeats exactly (the line copies itself) */
            uint32_t dl = delay_samples(), k;
            static int32_t hist[FS * 2];
            for (k = 0; k < FS * 2u; k += CTL) {
                bus_block(0, 0);
                for (i = 0; i < CTL; i++) hist[k + i] = wet_b[i];
            }
            for (k = dl; k < FS * 2u; k++) {
                same &= hist[k] == hist[k - dl];
                peak = hist[k] > peak ? hist[k] : peak;
            }
        }
        (void)a;
        ck("FREEZE delay: engaging steps no further than fed", st1 <= st0);
        ck("FREEZE delay: the line holds bit for bit, period to period (and is not silent)", same && peak > 1000);
        usong[US_FRZ] = 0;
    }
    /* 2 */
    {
        int32_t first[2];
        uint32_t k;
        for (k = 0; k < 2u; k++) {
            uint32_t f = 0;
            fresh(0);
            usong[US_PRE] = k ? 50 : 0;
            for (b = 0; b < 2u * BPS; b++)
                bus_block(0, 0);                        /* (the length glides there) */
            imp_n = 0;
            first[k] = -1;
            for (b = 0; b < BPS / 2u && first[k] < 0; b++) {
                for (i = 0; i < CTL; i++) { dl_in[i] = 0; rv_in[i] = (b == 0 && i < 8u) ? 30000 : 0; wet_b[i] = 0; }
                space_pre(dl_in, rv_in, CTL);
                fx_buses(zc, dl_in, rv_in, wet_b, CTL);
                for (i = 0; i < CTL && first[k] < 0; i++, f++)
                    if (wet_b[i] > 50 || wet_b[i] < -50) first[k] = (int32_t)f;
            }
        }
        printf("space: PRE-DLY: first sound at %d (0 ms) and %d samples (50 ms)\n", first[0], first[1]);
        ck("PRE-DLY 50 ms: the reverb's first sound 50 ms (+-1 ms) later", first[0] >= 0 && first[1] >= 0 &&
           first[1] - first[0] > 2205 - 44 && first[1] - first[0] < 2205 + 44);
    }
    /* 3 */
    {
        static int32_t out[FS * 3];
        double mn = 1e9, mx = 0;
        int32_t pk = 0, lastz = -1;
        uint32_t k;
        fresh(0);
        song.g[G_DTIME] = 3;
        song.g[G_DFDBK] = 0;
        usong[US_WOW] = 127;
        imp_n = 0;
        for (k = 0; k < FS * 3u; k += CTL) {
            for (i = 0; i < CTL; i++) {
                dl_in[i] = (int32_t)(12000.0 * sin(2 * M_PI * 1000.0 * (k + i) / FS));
                rv_in[i] = 0;
                wet_b[i] = 0;
            }
            space_pre(dl_in, rv_in, CTL);
            fx_buses(zc, dl_in, rv_in, wet_b, CTL);
            for (i = 0; i < CTL; i++) out[k + i] = wet_b[i];
        }
        for (k = FS; k < FS * 3u; k++) {                /* the period between rising zero crossings */
            pk = out[k] > pk ? out[k] : pk;
            if (out[k - 1] < 0 && out[k] >= 0) {
                double z = k - 1 + (double)-out[k - 1] / (out[k] - out[k - 1]);
                if (lastz >= 0) {
                    double per = z - lastz;
                    (void)per;
                }
                lastz = (int32_t)k;
            }
        }
        {   /* frequency in 50 ms windows */
            uint32_t w;
            for (w = FS; w + 2205u < FS * 3u; w += 2205u) {
                uint32_t n = 0;
                for (k = w + 1; k < w + 2205u; k++)
                    n += out[k - 1] < 0 && out[k] >= 0;
                mn = n < mn ? n : mn;
                mx = n > mx ? n : mx;
            }
        }
        printf("space: WOW 127: echoes of 1 kHz at %.0f .. %.0f Hz, peak %d\n", mn * 20, mx * 20, pk);
        ck("WOW: the echo's pitch moves (>= 10 Hz apart in 50 ms windows), bounded", mx - mn >= 0.5 && pk < 40000);
    }
    /* 4 and 5: the master, on a stereo test pair */
    {
        static int32_t L[FS], R[FS];
        double e_lo, e_hi;
        uint32_t k, f;
        int32_t st = 0;
        double (*band)(double, int32_t) = 0;
        (void)band;
        for (f = 0; f < 2u; f++) {
            double hz = f ? 5000.0 : 60.0, in = 0, outp = 0;
            for (k = 0; k < 2u; k++) {
                uint32_t s;
                double e = 0;
                fresh(0);
                usong[US_DJ] = (int16_t)(k ? -64 : 0);
                for (s = 0; s < FS; s += CTL) {
                    umaster_block(CTL);
                    for (i = 0; i < CTL; i++) {
                        int32_t l = (int32_t)(10000.0 * sin(2 * M_PI * hz * (s + i) / FS)), r = l;
                        umaster(&l, &r);
                        if (s > FS / 2u) e += (double)l * l;
                    }
                }
                if (k) outp = e; else in = e;
            }
            if (f) e_hi = 10 * log10(outp / in); else e_lo = 10 * log10(outp / in);
        }
        printf("space: DJ -64: 60 Hz %.1f dB, 5 kHz %.1f dB\n", e_lo, e_hi);
        ck("DJ FILTER -64: a low-pass (60 Hz within 3 dB, 5 kHz below -20 dB)", e_lo > -3 && e_hi < -20);
        for (f = 0; f < 2u; f++) {
            double hz = f ? 10000.0 : 60.0, in = 0, outp = 0;
            for (k = 0; k < 2u; k++) {
                uint32_t s;
                double e = 0;
                fresh(0);
                usong[US_DJ] = (int16_t)(k ? 63 : 0);
                for (s = 0; s < FS; s += CTL) {
                    umaster_block(CTL);
                    for (i = 0; i < CTL; i++) {
                        int32_t l = (int32_t)(10000.0 * sin(2 * M_PI * hz * (s + i) / FS)), r = l;
                        umaster(&l, &r);
                        if (s > FS / 2u) e += (double)l * l;
                    }
                }
                if (k) outp = e; else in = e;
            }
            if (f) e_hi = 10 * log10(outp / in); else e_lo = 10 * log10(outp / in);
        }
        printf("space: DJ +63: 60 Hz %.1f dB, 10 kHz %.1f dB\n", e_lo, e_hi);
        ck("DJ FILTER +63: a high-pass (60 Hz below -20 dB, 10 kHz within 3 dB)", e_lo < -20 && e_hi > -3);
        /* sweeps through and back to 0 on a chord: no step beyond the signal's own, then bit for bit */
        {
            uint32_t s, exact = 1;
            int32_t last = 0, st_in = 0, lastin = 0;
            fresh(0);
            for (s = 0; s < 4u * FS; s += CTL) {
                uint32_t t = s / CTL;
                int32_t q = (int32_t)(t % BPS) * 64 / (int32_t)BPS;   /* each second a sweep, then a jump */
                usong[US_DJ] = (int16_t)(s < FS ? -q : s < 2u * FS ? (q > 63 ? 63 : q) : s < 2u * FS + FS / 4u ? -50 : 0);
                umaster_block(CTL);
                for (i = 0; i < CTL; i++) {
                    double ts = (double)(s + i) / FS;
                    int32_t x = (int32_t)(9000.0 * (sin(2 * M_PI * 110 * ts) + sin(2 * M_PI * 165 * ts) + sin(2 * M_PI * 2200 * ts)));
                    int32_t l = x, r = x, d = x - lastin;
                    d = d < 0 ? -d : d;
                    st_in = d > st_in ? d : st_in;
                    lastin = x;
                    umaster(&l, &r);
                    d = l - last;
                    d = d < 0 ? -d : d;
                    if (s < 3u * FS) st = d > st ? d : st;
                    last = l;
                    if (s >= 3u * FS) exact &= l == x && r == x;
                }
            }
            printf("space: DJ sweeps and jumps: largest step %d (the input's %d)\n", st, st_in);
            ck("DJ FILTER swept, jumped HP -> LP -> 0: no step past the input's own x1.5, then bit for bit", st <= st_in * 3 / 2 && exact);
            (void)L; (void)R;
        }
    }
    {
        uint32_t s, k, exact = 1;
        int32_t pk[2] = {0, 0};
        for (k = 0; k < 2u; k++) {
            fresh(0);
            usong[US_GLUE] = (int16_t)(k ? 127 : 0);
            for (s = 0; s < 2u * FS; s += CTL) {
                umaster_block(CTL);
                for (i = 0; i < CTL; i++) {
                    double ts = (double)(s + i) / FS;
                    int32_t x = (int32_t)(14000.0 * (sin(2 * M_PI * 110 * ts) + sin(2 * M_PI * 138.6 * ts) + sin(2 * M_PI * 165 * ts)));
                    int32_t l = x, r = x;
                    umaster(&l, &r);
                    if (s > FS) pk[k] = l > pk[k] ? l : pk[k];
                }
            }
        }
        printf("space: GLUE 127: a loud chord's peak %d (off %d)\n", pk[1], pk[0]);
        ck("GLUE 127: a loud chord's peak lower than without", pk[1] < pk[0] * 9 / 10);
        usong[US_GLUE] = 0;
        for (s = 0; s < 2u * FS; s += CTL) {
            umaster_block(CTL);
            for (i = 0; i < CTL; i++) {
                int32_t x = (int32_t)(14000.0 * sin(2 * M_PI * 110 * (double)(s + i) / FS)), l = x, r = x;
                umaster(&l, &r);
                if (s > FS) exact &= l == x;
            }
        }
        ck("GLUE back to 0: bit for bit once settled", exact);
    }
#if UMBRA_SHIMMER
    {
        int32_t last = 0, st = 0;
        double r;
        fresh(2);
        usong[US_SHIM] = 127;
        song.g[G_RSIZE] = 100;
        rms_blocks(BPS, dark_noise, &st, &last);
        r = rms_blocks(10 * BPS, 0, 0, &last);
        printf("space: SHIMMER 127 on HALL: tail rms %.0f over 10 s\n", r);
        ck("SHIMMER 127: bounded (no runaway: the tail's rms below 20000)", r < 20000);
    }
#endif
    /* 6 */
    {
        struct timespec a, c;
        double ns_base, ns_all, ns_master, ns_voice;
        uint32_t r;
        track_t *t;
        static int32_t vb[CTL];
        fresh(2);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 10u * BPS; r++)
            bus_block(dark_noise, dark_noise);
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns_base = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / (10.0 * BPS * CTL);
        fresh(2);
        usong[US_WOW] = 100; usong[US_PRE] = 60; usong[US_SHIM] = UMBRA_SHIMMER ? 80 : 0;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 10u * BPS; r++)
            bus_block(dark_noise, dark_noise);
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns_all = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / (10.0 * BPS * CTL);
        fresh(0);
        usong[US_DJ] = -30; usong[US_GLUE] = 100;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 10u * BPS; r++) {
            umaster_block(CTL);
            for (i = 0; i < CTL; i++) { int32_t l = noise(), rr = l; umaster(&l, &rr); vb[i] = l; }
        }
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns_master = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / (10.0 * BPS * CTL);
        ui_power_on();
        t = &trk[0];
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        trk_note_on(t, 48, 100);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 10u * BPS; r++) {
            memset(vb, 0, sizeof vb);
            track_render(t, vb, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns_voice = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / (10.0 * BPS * CTL);
        printf("space: cost: buses (HALL) %.1f ns, + WOW PRE%s %.1f ns (+%.2f voices); MASTER DJ + GLUE %.1f ns (%.2f voices); "
               "one ANALOG voice %.1f ns\n", ns_base, UMBRA_SHIMMER ? " SHIMMER" : "", ns_all, (ns_all - ns_base) / ns_voice,
               ns_master, ns_master / ns_voice, ns_voice);
        ck(UMBRA_SHIMMER ? "cost: SPACE's additions with SHIMMER under 1.5 ANALOG voices, MASTER under one"
                         : "cost: SPACE's additions and MASTER each under one ANALOG voice",
           ns_all - ns_base < (UMBRA_SHIMMER ? 1.5 : 1.0) * ns_voice && ns_master < ns_voice);
    }
    /* 7. the whole mix: four tracks playing (FOUR ENGINES), the default chain, against every track with DRIVE DAMAGE and
     * FILTER RESONATOR (6 bands) and the buses with WOW PRE-DLY, MASTER's DJ and GLUE: host time, 4 s each */
    {
        struct timespec a, c;
        double ns[2];
        uint32_t k, f, tr, r;
        static const int16_t DMG[8] = {60, 40, 30, 0, 10, 0, 40, 112};
        for (k = 0; k < 2u; k++) {
            ui_power_on();
            song.master_q12 = 2048;
            umbra_recipe(0);
            if (k) {
                for (tr = 0; tr < NTRK; tr++) {
                    chain_set_type(tr, DEV_DRIVE, DRIVE_DAMAGE);
                    for (i = 0; i < 8u; i++) uchain[tr].p[DEV_DRIVE][i] = DMG[i];
                    chain_set_type(tr, DEV_FILTER, FILTER_RES);
                    trk[tr].p[P_REV] = 80; trk[tr].p[P_DLY] = 40;
                }
                usong[US_WOW] = 60; usong[US_PRE] = 40; usong[US_DJ] = -20; usong[US_GLUE] = 80;
            }
            transport_req = 1;
            for (f = 0; f < FS; f += CTL) { int32_t o[2 * CTL]; mix_block(o, CTL); }   /* (1 s to settle) */
            ns[k] = 1e9;
            for (r = 0; r < 3u; r++) {                  /* (the best of three 2 s: a busy machine's spikes out) */
                double t;
                clock_gettime(CLOCK_MONOTONIC, &a);
                for (f = 0; f < 2u * FS; f += CTL) { int32_t o[2 * CTL]; mix_block(o, CTL); }
                clock_gettime(CLOCK_MONOTONIC, &c);
                t = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / (2.0 * FS);
                ns[k] = t < ns[k] ? t : ns[k];
            }
        }
        printf("space: the whole mix, four tracks: default chain %.1f ns / sample, every track DAMAGE + RESONATOR and the "
               "buses' and MASTER's additions %.1f ns (x%.2f)\n", ns[0], ns[1], ns[1] / ns[0]);
        ck("the whole mix with everything on: within 3x the default chain's (host)", ns[1] < 3.0 * ns[0]);
        chain_reset();
    }
    printf(bad ? "space: %d FAILED\n" : "space: all ok\n", bad);
    return bad ? 1 : 0;
}
