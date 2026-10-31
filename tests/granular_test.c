/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA GRANULAR GRAINS (firmware/src/umbra_granular.c) on test signals, on the real sources:
 * 1. mu-law: encode / decode within a step (4 %, +-8 near 0) of the value, monotonic
 * 2. the ring: a value a pair, the write wrapping at UMBRA_GRN_RING, the valid part growing to it
 * 3. a sine through the cloud (PITCH 0, +12): it sounds, bounded, at the sine's pitch (an octave up), no click
 * 4. the pool under stress (4 tracks, DENSITY and SIZE at the top): never more than 12 grains (8 under load), 6 a
 *    track; the steals fade (no sample steps further than the busy cloud's own)
 * 5. SPEED 0: the ring stops recording, the cloud keeps sounding the held sound after its source goes quiet
 * 6. SCALE: every grain's pitch on the scale (PITCH RAND 12, C major)
 * 7. cost: host time per sample, a track's 6 grains and the full pool, against one ANALOG voice
 *   build/host/granular_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <time.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("granular: %-88s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void setp(uint32_t tr, uint32_t k, int32_t v) { uchain[tr].p[DEV_GRANULAR][k] = (int16_t)v; }
static void fresh(void)
{
    uint32_t tr;
    ui_power_on();
    memset(grn, 0, sizeof grn);
    memset(grs, 0, sizeof grs);
    memset(grn_ring, 0, sizeof grn_ring);
    grn_cap = GRN_MAX;
    for (tr = 0; tr < NTRK; tr++)
        chain_set_type(tr, DEV_GRANULAR, GRAN_GRAINS);
}
#define SECS(s) ((uint32_t)((s) * FS))
static int32_t blkbuf[CTL];
static int32_t pre_sine[4410];                          /* (the cost: 220 Hz precomputed, no libm in the timing) */
/* a sine (hz, amp; amp 0: silence) through track tr's GRAINS for n samples; out (optional), the largest step out */
static int32_t run_sine(uint32_t tr, double hz, int32_t amp, uint32_t n, uint32_t *ph, int32_t *out, int32_t *last)
{
    uint32_t f, i;
    int32_t worst = 0;
    for (f = 0; f < n; f += CTL) {
        if (hz < 0)                                     /* (hz < 0: the precomputed sine) */
            for (i = 0; i < CTL; i++, (*ph)++)
                blkbuf[i] = pre_sine[*ph % 4410u];
        else
            for (i = 0; i < CTL; i++, (*ph)++)
                blkbuf[i] = amp ? (int32_t)(amp * sin(2 * M_PI * hz * *ph / FS)) : 0;
        grn_run(tr, &trk[tr], (const int16_t *)uchain[tr].p[DEV_GRANULAR], blkbuf, CTL, 0, 0u);
        for (i = 0; i < CTL; i++) {
            int32_t d = blkbuf[i] - *last;
            d = d < 0 ? -d : d;
            worst = d > worst ? d : worst;
            *last = blkbuf[i];
            if (out)
                out[f + i] = blkbuf[i];
        }
    }
    return worst;
}
static double zc_hz(const int32_t *x, uint32_t n)       /* rising zero crossings a second */
{
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * FS / n;
}

int main(void)
{
    static int32_t out[SECS(2)];
    uint32_t i, ph = 0, g;
    int32_t last = 0, worst;
    /* 1 */
    {
        int ok = 1;
        int32_t x, prev = -999999;
        for (x = -32767; x <= 32767; x += 7) {
            int32_t y = ulaw_dec(ulaw_enc(x)), e = y - x, lim = (x < 0 ? -x : x) * 4 / 100 + 8;   /* (a step: ~3.2 %) */
            ok &= (e < 0 ? -e : e) <= lim && y >= prev;
            prev = y;
        }
        ck("mu-law: decode(encode(x)) within a step (4 %, +-8 near 0), monotonic", ok);
    }
    /* 2 */
    fresh();
    setp(0, GRN_DENS, 0);
    setp(0, GRN_MIX, 0);
    run_sine(0, 440, 10000, 2u * GRN_RL - 64u, &ph, 0, &last);   /* (GRN_RL - 32 values) */
    ck("the ring: a value a pair, not yet wrapped, the valid part = what was written",
       grs[0].w == GRN_RL - 32u && grs[0].filled == GRN_RL - 32u);
    run_sine(0, 440, 10000, 128u, &ph, 0, &last);
    ck("the ring: the write wraps at UMBRA_GRN_RING, the valid part stays the ring", grs[0].w == 32u && grs[0].filled == GRN_RL);
    /* 3 */
    {
        double hz[2];
        int32_t pk = 0, wsrc;
        uint32_t k;
        for (k = 0; k < 2u; k++) {
            fresh();
            ph = 0;
            last = 0;
            setp(0, GRN_PITCH, k ? 12 : 0);
            setp(0, GRN_SPRAY, 0);
            setp(0, GRN_SIZE, 90);
            setp(0, GRN_DENS, 60);
            run_sine(0, 220, 12000, SECS(0.7), &ph, 0, &last);   /* (the ring filled) */
            worst = run_sine(0, 220, 12000, SECS(1), &ph, out, &last);
            hz[k] = zc_hz(out, SECS(1));
            if (!k) {
                for (i = 0; i < SECS(1); i++)
                    pk = out[i] > pk ? out[i] : pk;
            }
            printf("granular: PITCH %d: %.0f Hz, peak %d, largest step %d\n", k ? 12 : 0, hz[k], pk, worst);
        }
        wsrc = (int32_t)(12000 * 2 * M_PI * 220 / FS) + 1;   /* the sine's own largest step */
        ck("a sine through the cloud: it sounds, bounded, at 220 Hz (+-10 %)", pk > 2000 && pk < 40000 && hz[0] > 198 && hz[0] < 242);
        ck("PITCH +12: an octave up (440 Hz +-10 %)", hz[1] > 396 && hz[1] < 484);
        ck("no click: no step past 4x the source's own (mu-law grain, windows, overlap)", worst < 4 * wsrc * 2);
    }
    /* 4 */
    {
        uint32_t tr, most = 0, most_tr = 0, steals = 0, b;
        int32_t worst4 = 0, lasts[NTRK] = {0};
        uint32_t phs[NTRK] = {0};
        fresh();
        for (tr = 0; tr < NTRK; tr++) {
            setp(tr, GRN_DENS, 127);
            setp(tr, GRN_SIZE, 127);
            setp(tr, GRN_SPRAY, 127);
            setp(tr, GRN_REV, 64);
        }
        for (b = 0; b < SECS(3) / CTL; b++) {
            if (b == SECS(2) / CTL)
                song.cpu_q8 = 230;                      /* (the last second under load: 8) */
            for (tr = 0; tr < NTRK; tr++) {
                uint32_t all, mine;
                grn_cap = song.cpu_q8 > 205u ? GRN_MAX_LOAD : grn_cap;
                worst = run_sine(tr, 110.0 * (tr + 1), 9000, CTL, &phs[tr], 0, &lasts[tr]);
                worst4 = worst > worst4 ? worst : worst4;
                mine = grn_count(tr, &all);
                most_tr = mine > most_tr ? mine : most_tr;
                most = all > most ? all : most;
            }
            for (g = 0; g < GRN_MAX; g++)
                steals += grn[g].on && grn[g].fading;
        }
        {
            uint32_t all;
            grn_count(0, &all);
            printf("granular: stress: at most %u grains (%u a track), %u fading block-grains seen, now %u (cap %u), "
                   "largest step %d\n", most, most_tr, steals, all, grn_cap, worst4);
            ck("the pool under stress: never more than 12, 6 a track; steals fade", most <= GRN_MAX && most_tr <= GRN_PER_TRK &&
               steals > 0 && worst4 < 60000);
            ck("under load (CPU > 80 %): 8 grains at most", grn_cap == GRN_MAX_LOAD && all <= GRN_MAX_LOAD);
        }
        song.cpu_q8 = 0;
    }
    /* 5 */
    {
        uint32_t w0;
        int32_t pk = 0;
        fresh();
        ph = 0;
        last = 0;
        setp(0, GRN_DENS, 80);
        run_sine(0, 330, 12000, SECS(0.7), &ph, 0, &last);
        setp(0, GRN_SPEED, 0);
        w0 = grs[0].w;
        run_sine(0, 330, 0, SECS(1), &ph, out, &last);   /* (the source silent) */
        for (i = SECS(0.5); i < SECS(1); i++)
            pk = out[i] > pk ? out[i] : pk;
        ck("SPEED 0: the ring stops, the cloud keeps the held sound after its source went quiet", grs[0].w == w0 && pk > 2000);
    }
    /* 6 */
    {
        int ok = 1;
        uint32_t n = 0;
        fresh();
        ph = 0;
        last = 0;
        trk[0].p[P_SCALE] = 1;                          /* C major, root C */
        trk[0].p[P_ROOT] = 0;
        setp(0, GRN_SCALE, 1);
        setp(0, GRN_PRAND, 12);
        setp(0, GRN_DENS, 127);
        setp(0, GRN_SIZE, 20);
        for (i = 0; i < 600u; i++) {
            run_sine(0, 220, 8000, CTL * 4u, &ph, 0, &last);
            for (g = 0; g < GRN_MAX; g++) {
                int32_t inc, s, best = 99;
                uint32_t k;
                if (!grn[g].on || grn[g].t > CTL * 4u)
                    continue;
                inc = grn[g].inc < 0 ? -grn[g].inc : grn[g].inc;
                for (k = 0; k < 73u; k++) {             /* the semitone whose ratio it is */
                    s = (int32_t)k - 36;
                    if ((grn_ratio_q16(s) >> 1) == inc)
                        best = s;
                }
                ok &= best != 99 && ((0xAB5u >> (uint32_t)((best % 12 + 12) % 12)) & 1u);   /* C major: 0 2 4 5 7 9 11 */
                n++;
            }
        }
        printf("granular: SCALE: %u grains checked\n", n);
        ck("SCALE (C major, PITCH RAND 12): every grain's pitch on the scale", ok && n > 50u);
    }
    /* 7 */
    {
        struct timespec a, c;
        double ns6, ns12, nsv;
        uint32_t r, tr;
        track_t *t;
        static int32_t vb[CTL];
        for (i = 0; i < 4410u; i++)
            pre_sine[i] = (int32_t)(9000 * sin(2 * M_PI * 220.0 * i / FS));
        fresh();
        setp(0, GRN_DENS, 127);
        setp(0, GRN_SIZE, 127);
        ph = 0;
        run_sine(0, -1, 9000, SECS(1), &ph, 0, &last);
        clock_gettime(CLOCK_MONOTONIC, &a);
        run_sine(0, -1, 9000, SECS(2), &ph, 0, &last);
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns6 = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(2);
        for (tr = 1; tr < NTRK; tr++) {
            setp(tr, GRN_DENS, 127);
            setp(tr, GRN_SIZE, 127);
        }
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < SECS(2) / CTL; r++)
            for (tr = 0; tr < NTRK; tr++)
                run_sine(tr, -1, 9000, CTL, &ph, 0, &last);
        clock_gettime(CLOCK_MONOTONIC, &c);
        ns12 = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(2);
        ui_power_on();
        t = &trk[0];
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        trk_note_on(t, 48, 100);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < SECS(2) / CTL; r++) {
            memset(vb, 0, sizeof vb);
            track_render(t, vb, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &c);
        nsv = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(2);
        printf("granular: cost: a track's 6 grains %.1f ns / sample (%.2f voices), the full pool (4 tracks) %.1f ns "
               "(%.2f voices); one ANALOG voice %.1f ns\n", ns6, ns6 / nsv, ns12, ns12 / nsv, nsv);
        ck("cost: the full pool within 5 ANALOG voices", ns12 < 5.0 * nsv);
    }
    /* an empty ring: its bytes are mu-law 0x00 (full scale negative): no grain reads them (4b: Dead Bells' start, a
     * SPEED 0 cloud over nothing recorded thumped at -20000) */
    {
        int32_t pk0 = 0, pk1 = 0;
        fresh();
        setp(0, GRN_DENS, 127), setp(0, GRN_SIZE, 110), setp(0, GRN_MIX, 127), setp(0, GRN_SPEED, 0);
        run_sine(0, 0, 0, SECS(0.5), &ph, out, &last);
        for (i = 0; i < SECS(0.5); i++)
            pk0 = abs(out[i]) > pk0 ? abs(out[i]) : pk0;
        fresh();
        setp(0, GRN_DENS, 127), setp(0, GRN_SIZE, 40), setp(0, GRN_MIX, 127);
        run_sine(0, 220, 1000, SECS(0.5), &ph, out, &last);
        for (i = 0; i < SECS(0.5); i++)
            pk1 = abs(out[i]) > pk1 ? abs(out[i]) : pk1;
        printf("granular: empty ring: SPEED 0 over nothing peak %d; a 1000 sine from the start peak %d\n", pk0, pk1);
        ck("an empty ring: SPEED 0 over nothing is silent, a quiet start stays quiet (no grain reads unwritten bytes)",
           pk0 == 0 && pk1 > 200 && pk1 < 2500);
    }
    chain_reset();
    printf(bad ? "granular: %d FAILED\n" : "granular: all ok\n", bad);
    return bad ? 1 : 0;
}
