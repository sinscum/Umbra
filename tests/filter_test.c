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

static void noise_in(int32_t amp)
{
    uint32_t i;
    int32_t r = 0x13579BDF;
    for (i = 0; i < LEN; i++)
        in[i] = (int32_t)(((int64_t)((int32_t)noise32(&r) >> 16) * amp) >> 15);
}
static void run_ty(uint32_t ty, const int16_t *p, flt_state_t *st, int16_t *cbuf)
{
    uint32_t i;
    memset(st, 0, sizeof *st);
    memset(cbuf, 0, UMBRA_COMB_LEN * sizeof cbuf[0]);
    memcpy(out, in, sizeof out);
    for (i = 0; i < LEN; i += CTL) {
        if (ty == FILTER_COMB) flt_comb(st, T, p, out + i, CTL, cbuf);
        else if (ty == FILTER_RES) flt_res(st, T, p, out + i, CTL);
        else flt_formant(st, T, p, out + i, CTL);
    }
}
static double goertzel(const int32_t *x, double hz)   /* energy at hz over the last second */
{
    double w = 2 * M_PI * hz / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = LEN - FS; i < LEN; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static int32_t peak_out(void)
{
    int32_t pk = 0;
    uint32_t i;
    for (i = 0; i < LEN; i++) { int32_t a = out[i] < 0 ? -out[i] : out[i]; if (a > pk) pk = a; }
    return pk;
}
static double mean_out(void)
{
    double sum = 0;
    uint32_t i;
    for (i = LEN - FS; i < LEN; i++) sum += out[i];
    return sum / FS;
}
static double zc_hz(const int32_t *x, uint32_t n)      /* frequency from upward zero crossings (interpolated) */
{
    double first = -1, last = -1;
    uint32_t i, k = 0;
    for (i = 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = i - 1 + (double)-x[i - 1] / (x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            k++;
        }
    return k > 1 ? (k - 1) * (double)FS / (last - first) : 0;
}
static double voice_ns;

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
        voice_ns = ns_v;
        printf("filter: cost: SVF %.1f ns / sample, one ANALOG voice %.1f ns: %.2f voices\n", ns_f, ns_v, ns_f / ns_v);
        ck("cost: SVF no more than one ANALOG voice", ns_f < ns_v);
    }
    /* ---- COMB, RESONATOR, FORMANT (Phase 2) ---- */
    {
        int16_t q[8];
        static int16_t cbuf[UMBRA_COMB_LEN];
        uint32_t ty;
        int32_t pk;
        int ok;
        /* MIX 0: bit for bit, each */
        tone(220, 20000);
        for (ty = FILTER_COMB; ty <= FILTER_FORMANT; ty++) {
            const param_desc_t *pd = ty == FILTER_COMB ? FLT_PD_COMB : ty == FILTER_RES ? FLT_PD_RES : FLT_PD_FORMANT;
            char what[96];
            for (i = 0; i < 8u; i++) q[i] = pd[i].def;
            q[3] = 0;                                    /* (MIX: the fourth value of each) */
            run_ty(ty, q, &st, cbuf);
            snprintf(what, sizeof what, "%s MIX 0: the input bit for bit", ty == FILTER_COMB ? "COMB" : ty == FILTER_RES ?
                     "RESONATOR" : "FORMANT");
            ck(what, !memcmp(out, in, sizeof out));
        }
        /* COMB on noise: its note (A3, 220 Hz) and its harmonics stand out; POL -: the odd ones of 110 Hz */
        noise_in(20000);
        for (i = 0; i < 8u; i++) q[i] = FLT_PD_COMB[i].def;
        q[CMB_NOTE] = 57; q[CMB_FDBK] = 120;
        run_ty(FILTER_COMB, q, &st, cbuf);
        ck("COMB on noise: 220 Hz and 440 Hz over 330 Hz (its harmonics stand out)",
           goertzel(out, 220) > 3 * goertzel(out, 330) && goertzel(out, 440) > 3 * goertzel(out, 330));
        q[CMB_POL] = 1;
        run_ty(FILTER_COMB, q, &st, cbuf);
        ck("COMB POL -: 110 Hz and 330 Hz over 220 Hz (odd harmonics, an octave down)",
           goertzel(out, 110) > 3 * goertzel(out, 220) && goertzel(out, 330) > 3 * goertzel(out, 220));
        /* bounded: every type at its most resonant on a loud square */
        for (i = 0; i < LEN; i++) in[i] = (i / 100u) & 1u ? 200000 : -200000;
        ok = 1;
        for (ty = FILTER_COMB; ty <= FILTER_FORMANT; ty++) {
            const param_desc_t *pd = ty == FILTER_COMB ? FLT_PD_COMB : ty == FILTER_RES ? FLT_PD_RES : FLT_PD_FORMANT;
            for (i = 0; i < 8u; i++) q[i] = pd[i].def;
            q[1] = 127;                                  /* FDBK / RING / RES at the top */
            run_ty(ty, q, &st, cbuf);
            pk = peak_out();
            if (pk > 262142 || mean_out() > 3000 || mean_out() < -3000) {
                printf("  type %u: peak %d mean %.1f\n", ty, pk, mean_out());
                ok = 0;
            }
        }
        ck("COMB, RESONATOR, FORMANT at their most resonant on a loud square: bounded, no DC", ok);
        /* their rings end */
        ok = 1;
        for (ty = FILTER_COMB; ty <= FILTER_FORMANT; ty++) {
            const param_desc_t *pd = ty == FILTER_COMB ? FLT_PD_COMB : ty == FILTER_RES ? FLT_PD_RES : FLT_PD_FORMANT;
            uint32_t blk = 0;
            for (i = 0; i < 8u; i++) q[i] = pd[i].def;
            noise_in(20000);
            run_ty(ty, q, &st, cbuf);
            ok &= flt_ringing(&st);
            while (flt_ringing(&st) && blk < 30u * FS / CTL) {
                memset(out, 0, CTL * sizeof out[0]);
                if (ty == FILTER_COMB) flt_comb(&st, T, q, out, CTL, cbuf);
                else if (ty == FILTER_RES) flt_res(&st, T, q, out, CTL);
                else flt_formant(&st, T, q, out, CTL);
                blk++;
            }
            printf("filter: type %u rings %.2f s at its defaults\n", ty, blk * (double)CTL / FS);
            ok &= !flt_ringing(&st);
        }
        ck("each rings after its input and falls silent (within 30 s)", ok);
        /* RESONATOR: in tune (one band: OCTAVE chord at MORPH 0 = every band on ROOT A2) */
        {
            double hz;
            for (i = 0; i < 8u; i++) q[i] = FLT_PD_RES[i].def;
            q[RSN_MORPH] = 0; q[RSN_RING] = 110;
            noise_in(60000);
            for (i = FS / 20u; i < LEN; i++) in[i] = 0;          /* a 50 ms burst of noise, then its ring */
            run_ty(FILTER_RES, q, &st, cbuf);
            hz = zc_hz(out + FS / 20u + 2000u, FS / 4u);       /* (the ring while it is strong: just after the burst) */
            printf("filter: RESONATOR ROOT A2 rings at %.2f Hz (110 Hz: %.1f cents)\n", hz, 1200 * log2(hz / 110.0));
            ck("RESONATOR in tune: ROOT A2 rings at 110 Hz within 5 cents", fabs(1200 * log2(hz / 110.0)) < 5);
        }
        /* RESONATOR MINOR on noise: its chord tones over a tone outside it */
        noise_in(20000);
        for (i = 0; i < 8u; i++) q[i] = FLT_PD_RES[i].def;   /* ROOT A2, MINOR, MORPH 64: A2 C3 E3 A3 C4 E4 */
        run_ty(FILTER_RES, q, &st, cbuf);
        ck("RESONATOR MINOR on noise: A2 and C3 (110, 130.8 Hz) over B2 (123.5 Hz)",
           goertzel(out, 110) > 3 * goertzel(out, 123.47) && goertzel(out, 130.81) > 3 * goertzel(out, 123.47));
        /* SCALE: the track's scale (C major from C): bands C E G C' E' G' */
        T->p[P_ROOT] = 0; T->p[P_SCALE] = 1;
        {
            uint32_t mask = scale_mask(T);
            int32_t want[FLT_BANDS] = {0, 4, 7, 12, 16, 19}, okk = 1;
            for (i = 0; i < FLT_BANDS; i++) okk &= rsn_interval(T, TUNE_SCALE, i, 48) == want[i];
            ck("RESONATOR SCALE: the track's scale from ROOT's degree (C major: C E G C E G)", okk || mask == 0xFFFu);
        }
        /* under load: four bands */
        flt_res_bands = 4;
        noise_in(20000);
        run_ty(FILTER_RES, q, &st, cbuf);
        ck("RESONATOR under load: four bands (A3 kept, E4 gone)",
           goertzel(out, 220) > 3 * goertzel(out, 329.63 * 2));
        flt_res_bands = FLT_BANDS;
        /* FORMANT: A (730 Hz first formant) against I (270 Hz) */
        {
            double a730, a270, i730, i270;
            noise_in(20000);
            for (i = 0; i < 8u; i++) q[i] = FLT_PD_FORMANT[i].def;
            q[FMT_VOWEL] = 0;
            run_ty(FILTER_FORMANT, q, &st, cbuf);
            a730 = goertzel(out, 730); a270 = goertzel(out, 270);
            q[FMT_VOWEL] = 64;                           /* (I: the third of five, 2 x 127 / 4) */
            run_ty(FILTER_FORMANT, q, &st, cbuf);
            i730 = goertzel(out, 730); i270 = goertzel(out, 270);
            ck("FORMANT: A rings at 730 Hz more than at 270 Hz, I the other way", a730 > a270 && i270 > i730);
        }
        /* cost */
        {
            struct timespec ta, tb;
            double ns[3];
            for (ty = FILTER_COMB; ty <= FILTER_FORMANT; ty++) {
                const param_desc_t *pd = ty == FILTER_COMB ? FLT_PD_COMB : ty == FILTER_RES ? FLT_PD_RES : FLT_PD_FORMANT;
                uint32_t rr;
                for (i = 0; i < 8u; i++) q[i] = pd[i].def;
                q[3] = 90;
                noise_in(20000);
                memset(&st, 0, sizeof st);
                clock_gettime(CLOCK_MONOTONIC, &ta);
                for (rr = 0; rr < 10u; rr++) {
                    memcpy(out, in, sizeof out);
                    for (i = 0; i < LEN; i += CTL) {
                        if (ty == FILTER_COMB) flt_comb(&st, T, q, out + i, CTL, cbuf);
                        else if (ty == FILTER_RES) flt_res(&st, T, q, out + i, CTL);
                        else flt_formant(&st, T, q, out + i, CTL);
                    }
                }
                clock_gettime(CLOCK_MONOTONIC, &tb);
                ns[ty - FILTER_COMB] = ((tb.tv_sec - ta.tv_sec) * 1e9 + (tb.tv_nsec - ta.tv_nsec)) / (10.0 * LEN);
            }
            printf("filter: cost: COMB %.1f, RESONATOR %.1f, FORMANT %.1f ns / sample (ANALOG voice %.1f): %.2f %.2f %.2f "
                   "voices\n", ns[0], ns[1], ns[2], voice_ns, ns[0] / voice_ns, ns[1] / voice_ns, ns[2] / voice_ns);
            ck("cost: COMB under one ANALOG voice, FORMANT under 1.5, RESONATOR (6 bands) under three",
               ns[0] < voice_ns && ns[2] < 1.5 * voice_ns && ns[1] < 3 * voice_ns);
        }
    }
    printf(bad ? "filter: %d FAILED\n" : "filter: all ok\n", bad);
    return bad ? 1 : 0;
}
