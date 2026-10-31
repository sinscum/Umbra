/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA FILTER (firmware/src/umbra_filter.c) on test signals, on the real sources:
 * SVF 1. MIX 0: the input bit for bit
 *     2. the responses: LP keeps a low tone and cuts a high one, HP the other way, BP and NOTCH at FREQ
 *     3. RES at its top on a loud square, FREQ swept over the whole range: bounded, no DC, settles (no runaway)
 *     4. its ring ends: flt_ringing after the input, then not
 *     5. KEYTRK: a higher note, a higher cutoff; WANDER moves the cutoff, within its +-24 steps
 *     6. cost against one ANALOG voice
 *   build/host/filter_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <time.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("filter: %-83s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define LEN (2u * FS)
static int32_t in[LEN], out[LEN];
static track_t *T;

static void tone(double hz, double amp)
{
    uint32_t i;
    for (i = 0; i < LEN; i++)
        in[i] = (int32_t)(amp * sin(2 * M_PI * hz * i / FS));
}
static void svf_defaults(int16_t *p)
{
    uint32_t k;
    for (k = 0; k < 8u; k++)
        p[k] = FLT_PD_SVF[k].def;
}
/* in -> out through the SVF (values p), a fresh state; the RMS of the last second */
static double run(const int16_t *p, flt_state_t *st)
{
    uint32_t i;
    double e = 0;
    memset(st, 0, sizeof *st);
    memcpy(out, in, sizeof out);
    for (i = 0; i < LEN; i += CTL)
        flt_svf(st, T, p, out + i, CTL);
    for (i = LEN - FS; i < LEN; i++)
        e += (double)out[i] * out[i];
    return sqrt(e / FS);
}
static double rms_in(void)
{
    double e = 0;
    uint32_t i;
    for (i = LEN - FS; i < LEN; i++)
        e += (double)in[i] * in[i];
    return sqrt(e / FS);
}

int main(void)
{
    int16_t p[8];
    flt_state_t st;
    double r, lo, hi;
    uint32_t i;
    ui_power_on();
    T = &trk[0];

    /* 1 */
    tone(220, 20000);
    svf_defaults(p);
    p[SVF_MIX] = 0;
    run(p, &st);
    ck("SVF MIX 0: the input bit for bit", !memcmp(out, in, sizeof out));

    /* 2: FREQ 64 (~740 Hz), RES low */
    svf_defaults(p);
    p[SVF_FREQ] = 64;
    p[SVF_RES] = 10;
    tone(110, 20000); lo = run(p, &st) / rms_in();
    tone(6000, 20000); hi = run(p, &st) / rms_in();
    ck("LP (MORPH 0): 110 Hz kept (> 0.8), 6 kHz cut (< 0.1)", lo > 0.8 && hi < 0.1);
    p[SVF_MORPH] = 85;                                  /* HP */
    tone(110, 20000); lo = run(p, &st) / rms_in();
    tone(6000, 20000); hi = run(p, &st) / rms_in();
    ck("HP (MORPH 85): 110 Hz cut (< 0.1), 6 kHz kept (> 0.8)", lo < 0.1 && hi > 0.8);
    p[SVF_MORPH] = 42;                                  /* BP */
    tone(CUTOFF_HZ[64], 20000); r = run(p, &st) / rms_in();
    tone(110, 20000); lo = run(p, &st) / rms_in();
    ck("BP (MORPH 42): at FREQ more than far below it", r > 2 * lo);
    p[SVF_MORPH] = 127;                                 /* NOTCH */
    tone(CUTOFF_HZ[64], 20000); r = run(p, &st) / rms_in();
    tone(110, 20000); lo = run(p, &st) / rms_in();
    ck("NOTCH (MORPH 127): at FREQ cut (< 0.3), far below kept (> 0.8)", r < 0.3 && lo > 0.8);

    /* 3 */
    {
        int32_t pk = 0;
        double sum = 0;
        int ok = 1;
        for (i = 0; i < LEN; i++)
            in[i] = (i / 100u) & 1u ? 120000 : -120000;   /* a loud square, ~220 Hz */
        svf_defaults(p);
        p[SVF_RES] = 127;
        memset(&st, 0, sizeof st);
        memcpy(out, in, sizeof out);
        for (i = 0; i < LEN; i += CTL) {
            p[SVF_FREQ] = (int16_t)(i * 127u / LEN);       /* swept up across the two seconds */
            flt_svf(&st, T, p, out + i, CTL);
        }
        for (i = 0; i < LEN; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            if (a > pk) pk = a;
            if (i >= LEN - FS) sum += out[i];
        }
        ok = pk <= 262142 && sum / FS > -2000 && sum / FS < 2000;
        if (!ok) printf("  peak %d mean %.1f\n", pk, sum / FS);
        ck("RES at its top, a loud square, FREQ swept: bounded, no DC", ok);
    }

    /* 4 */
    {
        uint32_t blocks = 0;
        int rang;
        tone(220, 30000);
        svf_defaults(p);
        p[SVF_RES] = 120;
        run(p, &st);
        rang = flt_ringing(&st);
        memset(out, 0, CTL * sizeof out[0]);
        while (flt_ringing(&st) && blocks < 44100u / CTL) {
            memset(out, 0, CTL * sizeof out[0]);
            flt_svf(&st, T, p, out, CTL);
            blocks++;
        }
        printf("filter: ring at RES 120: %u blocks (%.0f ms)\n", blocks, blocks * CTL * 1000.0 / FS);
        ck("its ring: ringing after the input, silent within a second", rang && !flt_ringing(&st));
    }

    /* 5 */
    {
        int32_t c60, c84, lo2 = 1 << 30, hi2 = -(1 << 30);
        flt_state_t a;
        svf_defaults(p);
        p[SVF_KTRK] = 127;
        memset(&a, 0, sizeof a);
        c60 = flt_cut_target(&a, T, p);
        trk_note_on(T, 84, 100);
        c84 = flt_cut_target(&a, T, p);
        trk_note_off(T, 84);
        ck("KEYTRK 100 %: two octaves up, ~24 semitones' steps higher", c84 - c60 > 24 * 280 && c84 - c60 < 24 * 320);
        svf_defaults(p);
        p[SVF_FREQ] = 64;
        p[SVF_WANDER] = 127;
        memset(&a, 0, sizeof a);
        a.rng = 12345;
        for (i = 0; i < 20u * FS / CTL; i++) {           /* 20 s */
            int32_t c = flt_cut_target(&a, T, p);
            if (c < lo2) lo2 = c;
            if (c > hi2) hi2 = c;
        }
        ck("WANDER 100 %: the cutoff moves over 20 s, within +-24 steps", hi2 - lo2 > 8 * 256 &&
           lo2 >= (64 - 25) * 256 && hi2 <= (64 + 25) * 256);
    }

    /* 6 */
    {
        struct timespec ta, tb;
        double ns_f, ns_v;
        uint32_t rr;
        static int32_t vb[CTL];
        tone(220, 20000);
        svf_defaults(p);
        p[SVF_MORPH] = 60;
        p[SVF_MIX] = 90;
        memset(&st, 0, sizeof st);
        clock_gettime(CLOCK_MONOTONIC, &ta);
        for (rr = 0; rr < 20u; rr++) {
            memcpy(out, in, sizeof out);
            for (i = 0; i < LEN; i += CTL)
                flt_svf(&st, T, p, out + i, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &tb);
        ns_f = ((tb.tv_sec - ta.tv_sec) * 1e9 + (tb.tv_nsec - ta.tv_nsec)) / (20.0 * LEN);
        set_engine_of(T, 0);
        T->engine = T->eng_req;
        trk_note_on(T, 48, 100);
        clock_gettime(CLOCK_MONOTONIC, &ta);
        for (rr = 0; rr < 20u; rr++)
            for (i = 0; i < LEN; i += CTL) {
                memset(vb, 0, sizeof vb);
                track_render(T, vb, CTL);
            }
        clock_gettime(CLOCK_MONOTONIC, &tb);
        ns_v = ((tb.tv_sec - ta.tv_sec) * 1e9 + (tb.tv_nsec - ta.tv_nsec)) / (20.0 * LEN);
        printf("filter: cost: SVF %.1f ns / sample, one ANALOG voice %.1f ns: %.2f voices\n", ns_f, ns_v, ns_f / ns_v);
        ck("cost: SVF no more than one ANALOG voice", ns_f < ns_v);
    }
    printf(bad ? "filter: %d FAILED\n" : "filter: all ok\n", bad);
    return bad ? 1 : 0;
}
