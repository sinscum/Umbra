/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA DRIVE DAMAGE (firmware/src/umbra_drive.c) on test signals, on the real sources:
 * 1. at its defaults: the input bit for bit (a sine, a loud chord)
 * 2. each stage alone at its top, then all at once: changes the sound, stays within +-2^18, no DC (mean), no NaN
 *    (fixed point), COMP lowers a loud signal's peak relative to its input
 * 3. a DRIVE jump 0 -> 127 between blocks: no sample steps further than the static setting's worst step
 * 4. TILT: + brighter, - darker (high band's energy against low band's)
 * 5. cost: host time per sample, all stages at their top, against one ANALOG voice's render
 *   build/host/drive_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <time.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("drive: %-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define LEN (2u * FS)                                 /* two seconds; DC measured over the last, whole cycles of every tone */
static int32_t in_sine[LEN], in_loud[LEN], out[LEN];

static void make_inputs(void)
{
    uint32_t i;
    for (i = 0; i < LEN; i++) {
        double t = (double)i / FS;
        in_sine[i] = (int32_t)(16000.0 * sin(2 * M_PI * 220.0 * t));
        in_loud[i] = (int32_t)(30000.0 * (sin(2 * M_PI * 110.0 * t) + sin(2 * M_PI * 165.0 * t) +
                                          sin(2 * M_PI * 220.0 * t) + 0.5 * sin(2 * M_PI * 3300.0 * t)));
    }
}

/* in -> out through DAMAGE with values p, blocks of CTL, a fresh state; peak and mean of out */
static void run(const int32_t *in, const int16_t *p, int32_t *peak, double *mean)
{
    drv_state_t st;
    uint32_t i;
    double sum = 0;
    memset(&st, 0, sizeof st);
    memcpy(out, in, sizeof out);
    for (i = 0; i < LEN; i += CTL)
        drv_damage(&st, p, out + i, CTL);
    *peak = 0;
    for (i = 0; i < LEN; i++) {
        int32_t a = out[i] < 0 ? -out[i] : out[i];
        if (a > *peak) *peak = a;
        if (i >= LEN - FS)
            sum += out[i];
    }
    *mean = sum / FS;                               /* (1 s: 110 165 220 3300 Hz whole cycles, a mean of 0 in) */
}
static void defaults(int16_t *p)
{
    uint32_t k;
    for (k = 0; k < 8u; k++)
        p[k] = DRV_PD_DAMAGE[k].def;
}
static double band(const int32_t *x, int hi)        /* energy above (hi) or below ~2 kHz: a one-pole split */
{
    double lp = 0, e = 0;
    uint32_t i;
    for (i = 0; i < LEN; i++) {
        double h;
        lp += (x[i] - lp) * 0.25;
        h = x[i] - lp;
        e += hi ? h * h : lp * lp;
    }
    return e;
}

int main(void)
{
    int16_t p[8];
    int32_t pk, pk0;
    double mean;
    uint32_t k;
    make_inputs();

    /* 1 */
    defaults(p);
    run(in_sine, p, &pk, &mean);
    ck("defaults: a sine bit for bit", !memcmp(out, in_sine, sizeof out));
    run(in_loud, p, &pk, &mean);
    ck("defaults: a loud chord bit for bit", !memcmp(out, in_loud, sizeof out));

    /* 2 */
    {
        static const char *const N[8] = {"DRIVE", "FOLD", "CRUSH", "DECIM", "NOISE", "TILT", "COMP", "OUT"};
        int ok = 1;
        for (k = 0; k < 8u; k++) {
            if (k == DRV_OUT)
                continue;
            defaults(p);
            p[k] = k == DRV_TILT ? 63 : 127;
            run(in_loud, p, &pk, &mean);
            if (!(memcmp(out, in_loud, sizeof out) && pk <= DRV_LIM && mean > -300 && mean < 300)) {
                printf("  %s at its top: peak %d mean %.1f\n", N[k], pk, mean);
                ok = 0;
            }
        }
        ck("each stage alone at its top: changes the sound, within +-2^18, no DC (|mean| < 300)", ok);
        for (k = 0; k < 8u; k++)
            p[k] = k == DRV_TILT ? -64 : k == DRV_OUT ? 127 : 127;
        run(in_loud, p, &pk, &mean);
        ck("every stage at its top: within +-2^18, no DC", pk <= DRV_LIM && mean > -300 && mean < 300);
        defaults(p);
        run(in_loud, p, &pk0, &mean);
        p[DRV_COMP] = 127;
        run(in_loud, p, &pk, &mean);
        ck("COMP at its top: a loud chord's peak lower than its input's (make-up included)", pk < pk0);
        defaults(p);
        p[DRV_OUT] = 0;
        run(in_loud, p, &pk, &mean);
        ck("OUT 0: silence", pk == 0);
    }

    /* 3 */
    {
        drv_state_t st;
        int32_t worst_static = 0, worst_jump = 0, prev;
        uint32_t i;
        defaults(p);
        p[DRV_DRIVE] = 127;
        run(in_sine, p, &pk, &mean);
        for (i = 1; i < LEN; i++) {
            int32_t d = out[i] - out[i - 1];
            if (d < 0) d = -d;
            if (d > worst_static) worst_static = d;
        }
        memset(&st, 0, sizeof st);
        memcpy(out, in_sine, sizeof out);
        defaults(p);
        for (i = 0; i < LEN; i += CTL) {
            if (i == 64u * CTL) p[DRV_DRIVE] = 127;   /* the knob jumps between blocks */
            drv_damage(&st, p, out + i, CTL);
        }
        prev = out[0];
        for (i = 1; i < LEN; i++) {
            int32_t d = out[i] - prev;
            if (d < 0) d = -d;
            if (d > worst_jump) worst_jump = d;
            prev = out[i];
        }
        ck("DRIVE 0 -> 127 at once: no sample steps further than at 127 throughout (no click)", worst_jump <= worst_static);
    }

    /* 4 */
    {
        double hi0, lo0, hiu, lou, hid, lod;
        defaults(p);
        run(in_loud, p, &pk, &mean);
        hi0 = band(out, 1); lo0 = band(out, 0);
        p[DRV_TILT] = 63;
        run(in_loud, p, &pk, &mean);
        hiu = band(out, 1); lou = band(out, 0);
        p[DRV_TILT] = -64;
        run(in_loud, p, &pk, &mean);
        hid = band(out, 1); lod = band(out, 0);
        ck("TILT: + brighter (more high band to low), - darker", hiu / lou > hi0 / lo0 && hid / lod < hi0 / lo0);
    }

    /* 5 */
    {
        drv_state_t st;
        struct timespec a, b;
        double ns_drv, ns_voice;
        uint32_t i, r;
        track_t *t;
        static int32_t vb[CTL];
        for (k = 0; k < 8u; k++)
            p[k] = k == DRV_TILT ? 40 : 100;
        memset(&st, 0, sizeof st);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 20u; r++) {
            memcpy(out, in_loud, sizeof out);
            for (i = 0; i < LEN; i += CTL)
                drv_damage(&st, p, out + i, CTL);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ns_drv = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (20.0 * LEN);
        ui_power_on();
        t = &trk[0];
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        trk_note_on(t, 48, 100);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (r = 0; r < 20u; r++)
            for (i = 0; i < LEN; i += CTL) {
                memset(vb, 0, sizeof vb);
                track_render(t, vb, CTL);
            }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ns_voice = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (20.0 * LEN);
        printf("drive: cost: DAMAGE (all stages) %.1f ns / sample, one ANALOG voice %.1f ns: %.2f voices\n", ns_drv,
               ns_voice, ns_drv / ns_voice);
        ck("cost: DAMAGE at full no more than two ANALOG voices", ns_drv < 2.0 * ns_voice);
    }
    printf(bad ? "drive: %d FAILED\n" : "drive: all ok\n", bad);
    return bad ? 1 : 0;
}
