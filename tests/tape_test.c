/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA SOURCE RETURN and TAPE (firmware/src/umbra_source.c, umbra_tape.c), on the real sources:
 * 1. RETURN: track 2 is track 1's last block after its chain, at LEVEL (one block late); itself: silence
 * 2. TAPE's parts: one track all 40 KiB, two half each (apart), a change empties them
 * 3. a take (FROM SELF, LO): its length as recorded; LOOP plays it at its pitch, bounded, its ends without a click; HI
 *    within 0.1 % of the source, LO within a mu-law step; PITCH +12 an octave up; DIR REV the same pitch
 * 4. a take runs full: it stops at the part's end (REC still on: no more)
 * 5. KEYS: a note plays the take from START, C5 an octave up; let go, it fades out (REL) and ends
 * 6. SELF over a take: an overdub (louder with FDBK 127; FDBK 0 the new sound alone)
 * 7. CLEAR: the take and its voices gone at once, the memory emptied a chunk a block
 * 8. FROM MIX: the mix of the block before
 * 9. GRAINS on a TAPE in LO read the take (sound with an empty ring)
 * 10. cost: LOOP, KEYS (4 voices), host time per sample against one ANALOG voice
 *   build/host/tape_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <time.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("tape: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int16_t P[DEV_NP];
static void tp_defaults(void)
{
    uint32_t k;
    for (k = 0; k < DEV_NP; k++)
        P[k] = SRC_PD_TAPE[k].def;
}
static void fresh(uint8_t mask)
{
    ui_power_on();
    memset(tps, 0, sizeof tps);
    memset(tape_mem, 0xFF, sizeof tape_mem);
    memset(tape_mix, 0, sizeof tape_mix);
    tape_part = 0;
    tape_block(mask);
    tp_defaults();
}
#define SECS(s) ((uint32_t)((s) * FS))
static uint32_t ph;
/* n samples of a sine (hz, amp) as the track's engine block through track 0's TAPE; out (optional) */
static int32_t run(double hz, int32_t amp, uint32_t n, int32_t *out)
{
    int32_t b[CTL], worst = 0, last = 0;
    uint32_t f, i;
    for (f = 0; f < n; f += CTL) {
        tape_block(tape_part);
        for (i = 0; i < CTL; i++, ph++)
            b[i] = amp ? (int32_t)(amp * sin(2 * M_PI * hz * ph / FS)) : 0;
        tape_run(0, P, b, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t d = b[i] - last;
            d = d < 0 ? -d : d;
            worst = f && d > worst ? d : worst;
            last = b[i];
            if (out && f + i < n)
                out[f + i] = b[i];
        }
    }
    return worst;
}
static double zc_hz(const int32_t *x, uint32_t n)
{
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * FS / n;
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    uint32_t i;
    int32_t p = 0;
    for (i = 0; i < n; i++)
        p = (x[i] < 0 ? -x[i] : x[i]) > p ? (x[i] < 0 ? -x[i] : x[i]) : p;
    return p;
}
static double rms(const int32_t *x, uint32_t n)
{
    uint32_t i;
    double e = 0;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return sqrt(e / n);
}

int main(void)
{
    static int32_t out[SECS(2)];
    uint32_t i;
    int32_t worst;
    /* 1 */
    {
        int32_t b[CTL];
        int16_t rp[DEV_NP] = {1, 112};
        int ok;
        ui_power_on();
        for (i = 0; i < CTL; i++)
            b[i] = (int32_t)i * 300 - 4000;
        src_post(0, b, CTL);
        src_swap();
        src_return(1, rp, b, CTL);
        ok = 1;
        for (i = 0; i < CTL; i++)
            ok &= b[i] == (int32_t)i * 300 - 4000;
        rp[RET_FROM] = 2;
        src_return(1, rp, b, CTL);
        for (i = 0; i < CTL; i++)
            ok &= b[i] == 0;
        ck("RETURN: track 2 = track 1's last block after its chain at 0 dB; FROM itself: silence", ok);
    }
    /* 2 */
    fresh(1u);
    ck("parts: one TAPE track, all 40 KiB", tps[0].size == UMBRA_TAPE_BYTES && tps[0].base == 0);
    tape_block(1u | 4u);
    ck("parts: two, half each, apart (track order)", tps[0].size == UMBRA_TAPE_BYTES / 2u && tps[2].base == UMBRA_TAPE_BYTES / 2u &&
       tps[0].len == 0);
    /* 3 */
    {
        double hz, hzu, hzr;
        int32_t pk, wsrc = (int32_t)(12000 * 2 * M_PI * 220 / FS) + 1;
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        ph = 0;
        run(220, 12000, SECS(0.5), 0);
        P[TP_REC] = TREC_OFF;
        ck("a take FROM SELF: 0.5 s recorded (a value a pair: 11025 values)", tps[0].len >= 11000u && tps[0].len <= 11040u);
        worst = run(0, 0, SECS(1), out);
        hz = zc_hz(out, SECS(1));
        pk = peak(out, SECS(1));
        printf("tape: LOOP: %.1f Hz, peak %d, largest step %d (the source's %d)\n", hz, pk, worst, wsrc);
        ck("LOOP: the take at its pitch (220 Hz +-3 %), bounded, no click at its ends", hz > 213 && hz < 227 && pk > 10000 &&
           pk < 13000 && worst < 3 * wsrc);
        P[TP_PITCH] = 12;
        run(0, 0, SECS(1), out);
        hzu = zc_hz(out, SECS(1));
        P[TP_PITCH] = 0;
        P[TP_DIR] = TDIR_REV;
        run(0, 0, SECS(1), out);
        hzr = zc_hz(out, SECS(1));
        P[TP_DIR] = TDIR_FWD;
        printf("tape: PITCH +12 %.1f Hz, DIR REV %.1f Hz\n", hzu, hzr);
        ck("PITCH +12: an octave up; DIR REV: the same pitch", hzu > 425 && hzu < 455 && hzr > 213 && hzr < 227);
        {   /* HI and LO: a value of the take against the source's pair average */
            double eh = 0, el = 0, sx = 0;
            uint32_t q;
            for (q = 0; q < 2u; q++) {
                fresh(1u);
                P[TP_FROM] = TFROM_SELF;
                P[TP_QUAL] = (int16_t)(q ? TQ_LO : TQ_HI);
                P[TP_REC] = TREC_ON;
                ph = 0;
                run(220, 12000, SECS(0.2), 0);
                for (i = 100; i < 4000u; i++) {
                    double t0 = 2.0 * i, a = 12000 * sin(2 * M_PI * 220 * t0 / FS), c = 12000 * sin(2 * M_PI * 220 * (t0 + 1) / FS);
                    double want = ((int32_t)a / 2 + (int32_t)c / 2) / 2.0, got = tape_get(&tps[0], i), e = fabs(got - want);
                    if (q) el = e > el ? e : el; else eh = e > eh ? e : eh;
                    sx = fabs(want) > sx ? fabs(want) : sx;
                }
            }
            printf("tape: HI worst error %.1f, LO %.1f (of %.0f)\n", eh, el, sx);
            ck("HI: within 0.1 % of the source; LO: within a mu-law step (4 %)", eh <= sx * 0.001 + 2 && el <= sx * 0.04 + 8);
        }
    }
    /* 4 */
    fresh(1u);
    P[TP_FROM] = TFROM_SELF;
    P[TP_REC] = TREC_ON;
    run(220, 8000, SECS(2.2), 0);
    ck("a take runs full: it stops at the part's end (1.86 s LO), REC still on", tps[0].full && tps[0].len == UMBRA_TAPE_BYTES);
    /* 5 */
    {
        double hz0, hz1;
        uint32_t n0;
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        ph = 0;
        run(220, 12000, SECS(1), 0);
        P[TP_REC] = TREC_OFF;
        P[TP_MODE] = TMODE_KEYS;
        tape_keys(0, 60, 1);
        run(0, 0, SECS(0.4), out);
        hz0 = zc_hz(out + SECS(0.05), SECS(0.3));
        tape_keys(0, 60, 0);
        tape_keys(0, 72, 1);
        run(0, 0, SECS(0.2), out);
        hz1 = zc_hz(out + SECS(0.05), SECS(0.1));
        tape_keys(0, 72, 0);
        run(0, 0, SECS(0.6), out);
        n0 = 0;
        for (i = 0; i < TAPE_VOICES; i++)
            n0 += tps[0].v[i].on != 0;
        printf("tape: KEYS: C4 %.1f Hz, C5 %.1f Hz, voices after the release %u, the tail's peak %d\n", hz0, hz1, n0,
               peak(out + SECS(0.5), SECS(0.1)));
        ck("KEYS: C4 the take's pitch, C5 an octave up; let go, it fades out and ends", hz0 > 213 && hz0 < 227 && hz1 > 425 &&
           hz1 < 455 && n0 == 0 && peak(out + SECS(0.5), SECS(0.1)) == 0);
    }
    /* 6 */
    {
        double r1, r2, r3;
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        ph = 0;
        run(220, 8000, SECS(0.5), 0);
        P[TP_REC] = TREC_OFF;
        run(0, 0, SECS(0.5), out);
        r1 = rms(out, SECS(0.5));
        P[TP_REC] = TREC_ON;                            /* (SELF over a take: an overdub, FDBK 127) */
        P[TP_FDBK] = 127;
        run(330, 8000, SECS(0.5), 0);
        P[TP_REC] = TREC_OFF;
        run(0, 0, SECS(0.5), out);
        r2 = rms(out, SECS(0.5));
        P[TP_REC] = TREC_ON;
        P[TP_FDBK] = 0;                                 /* (FDBK 0: the new sound alone) */
        run(0, 0, SECS(0.6), 0);
        P[TP_REC] = TREC_OFF;
        run(0, 0, SECS(0.5), out);
        r3 = rms(out, SECS(0.5));
        printf("tape: overdub: rms %.0f -> %.0f (FDBK 127, another sine) -> %.0f (FDBK 0, silence over it)\n", r1, r2, r3);
        ck("SELF over a take: an overdub (louder with FDBK 127; FDBK 0 leaves the new sound alone)", r2 > r1 * 1.2 && r3 < r1 * 0.05);
    }
    /* 7 */
    {
        uint32_t b;
        int ok;
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        run(220, 8000, SECS(0.5), 0);
        P[TP_REC] = TREC_OFF;
        tape_clr_ui[0]++;
        tape_block(tape_part);
        ok = tps[0].len == 0 && tps[0].clr > 0u && tps[0].clr < UMBRA_TAPE_BYTES;   /* (a chunk this block) */
        for (b = 0; b < UMBRA_TAPE_BYTES / TAPE_CLR_CHUNK + 1u; b++)
            tape_block(tape_part);
        for (i = 0; i < UMBRA_TAPE_BYTES; i++)
            ok &= tape_mem[i] == 0xFF;
        run(0, 0, SECS(0.1), out);
        ok &= peak(out, SECS(0.1)) == 0;
        ck("CLEAR: the take gone at once (silent), the memory emptied a chunk a block", ok);
    }
    /* 8 */
    {
        int32_t l[CTL], r[CTL], w[CTL], b[CTL];
        uint32_t f;
        fresh(1u);
        P[TP_FROM] = TFROM_MIX;
        P[TP_REC] = TREC_ON;
        for (f = 0; f < SECS(0.3); f += CTL) {
            for (i = 0; i < CTL; i++) {
                int32_t x = (int32_t)(16000 * sin(2 * M_PI * 220.0 * (f + i) / FS));
                l[i] = r[i] = x * 4;
                w[i] = 0;
                b[i] = 0;
            }
            tape_block(tape_part);
            tape_run(0, P, b, CTL);
            tape_take_mix(l, r, w, CTL);
        }
        P[TP_REC] = TREC_OFF;
        run(0, 0, SECS(0.3), out);
        printf("tape: FROM MIX: %.1f Hz, peak %d\n", zc_hz(out, SECS(0.3)), peak(out, SECS(0.3)));
        ck("FROM MIX: the mix of the block before (a 220 Hz mix comes back)", zc_hz(out, SECS(0.3)) > 200 &&
           zc_hz(out, SECS(0.3)) < 240 && peak(out, SECS(0.3)) > 8000);
    }
    /* 9 */
    {
        int32_t b[CTL], pk = 0;
        uint32_t f;
        int16_t gp[DEV_NP];
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        ph = 0;
        run(220, 12000, SECS(0.5), 0);
        P[TP_REC] = TREC_OFF;
        memset(grn, 0, sizeof grn);
        memset(grs, 0, sizeof grs);
        for (i = 0; i < DEV_NP; i++)
            gp[i] = GRN_PD[i].def;
        gp[GRN_DENS] = 90;
        for (f = 0; f < SECS(0.5); f += CTL) {
            for (i = 0; i < CTL; i++)
                b[i] = 0;
            grn_run(0, &trk[0], gp, b, CTL, tape_mem + tps[0].base, tps[0].len);
            for (i = 0; i < CTL; i++)
                pk = (b[i] < 0 ? -b[i] : b[i]) > pk ? (b[i] < 0 ? -b[i] : b[i]) : pk;
        }
        ck("GRAINS on a TAPE in LO read the take (a cloud with an empty ring)", pk > 2000 && grs[0].filled == 0);
    }
    /* 10 */
    {
        struct timespec a, c;
        double nl, nk, nv;
        uint32_t r;
        static int32_t vb[CTL];
        track_t *t;
        fresh(1u);
        P[TP_FROM] = TFROM_SELF;
        P[TP_REC] = TREC_ON;
        run(220, 12000, SECS(1), 0);
        P[TP_REC] = TREC_OFF;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < SECS(2) / CTL; r++) {
            memset(vb, 0, sizeof vb);
            tape_run(0, P, vb, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &c);
        nl = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(2);
        P[TP_MODE] = TMODE_KEYS;
        for (i = 0; i < 4u; i++)
            tape_keys(0, 60 + i * 4, 1);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < SECS(0.8) / CTL; r++) {
            memset(vb, 0, sizeof vb);
            tape_run(0, P, vb, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &c);
        nk = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(0.8);
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
        nv = ((c.tv_sec - a.tv_sec) * 1e9 + (c.tv_nsec - a.tv_nsec)) / SECS(2);
        printf("tape: cost: LOOP %.1f ns / sample (%.2f voices), KEYS x4 %.1f ns (%.2f voices); one ANALOG voice %.1f ns\n",
               nl, nl / nv, nk, nk / nv, nv);
        ck("cost: LOOP under one ANALOG voice, KEYS x4 under two", nl < nv && nk < 2 * nv);
    }
    chain_reset();
    printf(bad ? "tape: %d FAILED\n" : "tape: all ok\n", bad);
    return bad ? 1 : 0;
}
