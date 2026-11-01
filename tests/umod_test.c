/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA MODS (firmware/src/umbra_mod.c), on the real sources:
 * 1. nothing set: no value moves (DEST OFF, or DEPTH 0)
 * 2. DEST: from OFF the knob passes over what the track's devices lack (SYNTH, THRU: nothing) and choices (an F_ENUM)
 * 3. SINE on SVF FREQ: the value swings around the stored one by DEPTH (63: the whole range, clamped), and is the stored
 *    one again after the block (the UI never sees a moved value); a mix_block leaves it as stored
 * 4. RATE: a cycle in blocks as the table says (127: 0.1 s; 0: 10 minutes), the display (10.0 m, 7.4 s, 0.1 s)
 * 5. the shapes: SQR two values, SAW rising, S&H steps once a cycle, DRIFT glides (no step), negative DEPTH upside down
 * 6. two MODS on one value: both move it, the stored value back after
 * 7. a type change: a DEST the new type lacks does nothing
 *   build/host/umod_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("umod: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define DEST(d, k) ((int16_t)((d) * DEV_NP + (k) + 1))
static void set(uint32_t tr, uint32_t s, int shape, int rate, int dest, int depth)
{
    umod[tr][s][UM_SHAPE] = (int16_t)shape;
    umod[tr][s][UM_RATE] = (int16_t)rate;
    umod[tr][s][UM_DEST] = (int16_t)dest;
    umod[tr][s][UM_DEPTH] = (int16_t)depth;
}
/* n blocks of track tr's MODS alone: the moved value of (d, k) each block into out (if any); its min / max */
static void run(uint32_t tr, uint32_t d, uint32_t k, uint32_t n, int16_t *out, int32_t *mn, int32_t *mx, int *restored)
{
    uint32_t i;
    int16_t keep = uchain[tr].p[d][k];
    *mn = 99999, *mx = -99999, *restored = 1;
    for (i = 0; i < n; i++) {
        int32_t v;
        umod_begin(&trk[tr]);
        v = uchain[tr].p[d][k];
        umod_end(&trk[tr]);
        *restored &= uchain[tr].p[d][k] == keep;
        if (out)
            out[i] = (int16_t)v;
        *mn = v < *mn ? v : *mn;
        *mx = v > *mx ? v : *mx;
    }
}

int main(void)
{
    static int16_t out[200000];
    int32_t mn, mx;
    int rs;
    uint32_t i;
    /* 1 */
    ui_power_on();
    chain_set_type(0, DEV_FILTER, FILTER_SVF);
    uchain[0].p[DEV_FILTER][SVF_FREQ] = 60;
    run(0, DEV_FILTER, SVF_FREQ, 500, 0, &mn, &mx, &rs);
    ck("defaults (DEST OFF): nothing moves", mn == 60 && mx == 60 && rs);
    set(0, 0, UMS_SINE, 127, DEST(DEV_FILTER, SVF_FREQ), 0);
    run(0, DEV_FILTER, SVF_FREQ, 500, 0, &mn, &mx, &rs);
    ck("DEPTH 0: nothing moves", mn == 60 && mx == 60 && rs);
    /* 2 */
    {
        const param_desc_t *d;
        int16_t *vp;
        int32_t v;
        song.sel = 0;
        d = umod_desc(UM_DEST, &vp);
        v = param_turn(d, 0, 1);
        ck("DEST from OFF: SYNTH and GRANULAR THRU have nothing, so FILTER SVF's FREQ next",
           d && vp == (int16_t *)&umod[0][0][UM_DEST] && v == DEST(DEV_FILTER, SVF_FREQ));
        chain_set_type(0, DEV_SOURCE, SRC_TAPE);
        v = param_turn(d, DEST(DEV_SOURCE, TP_REL), 1);
        ck("DEST passes over choices: TAPE's REL -> FDBK (MODE DIR FROM QUAL REC skipped)", v == DEST(DEV_SOURCE, TP_FDBK));
        {
            char val[12];
            const char *unit;
            param_format(d, DEST(DEV_FILTER, SVF_FREQ), val, &unit);
            ck("DEST shows the value's name and its device (FREQ F)", !strcmp(val, "FREQ") && !strcmp(unit, "F"));
            param_format(d, 0, val, &unit);
            ck("DEST OFF shows OFF", !strcmp(val, "OFF"));
        }
        chain_set_type(0, DEV_SOURCE, SRC_SYNTH);
    }
    /* 3 */
    set(0, 0, UMS_SINE, 127, DEST(DEV_FILTER, SVF_FREQ), 32);
    run(0, DEV_FILTER, SVF_FREQ, 400, 0, &mn, &mx, &rs);
    printf("umod: SINE DEPTH 32 on FREQ 60: %d .. %d\n", mn, mx);
    ck("SINE, DEPTH 32: FREQ 60 swings about +-63 (half of its 127 either way, clamped)", mn == 0 && mx >= 120 && rs);
    set(0, 0, UMS_SINE, 127, DEST(DEV_FILTER, SVF_FREQ), 8);
    run(0, DEV_FILTER, SVF_FREQ, 400, 0, &mn, &mx, &rs);
    printf("umod: SINE DEPTH 8 on FREQ 60: %d .. %d\n", mn, mx);
    ck("SINE, DEPTH 8: +-15 or so around 60, the stored value back after each block", mn >= 42 && mn <= 47 && mx >= 73 &&
       mx <= 78 && rs);
    for (i = 0; i < 300; i++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    ck("a mix_block leaves the stored value (the UI sees 60)", uchain[0].p[DEV_FILTER][SVF_FREQ] == 60);
    /* 4 */
    {
        uint32_t c = 0, first = 0, second = 0;
        char val[12];
        const char *unit;
        set(0, 0, UMS_SAW, 127, DEST(DEV_FILTER, SVF_FREQ), 63);
        umod_st[0][0].ph = 0;
        run(0, DEV_FILTER, SVF_FREQ, 400, out, &mn, &mx, &rs);
        for (i = 1; i < 400 && !second; i++)
            if (out[i] < out[i - 1] - 30) {                 /* a saw's drop: a new cycle */
                if (!first) first = i; else second = i;
            }
        c = second - first;
        printf("umod: RATE 127: a cycle %u blocks (%u expected: 0.1 s)\n", c, 138u);
        ck("RATE 127: a cycle 0.1 s (138 blocks)", c >= 137 && c <= 139);
        param_format(&UMOD_PD[UM_RATE], 0, val, &unit);
        ck("RATE 0 reads 10.0 m", !strcmp(val, "10.0") && !strcmp(unit, "m"));
        param_format(&UMOD_PD[UM_RATE], 64, val, &unit);
        ck("RATE 64 reads 7.4 s", !strcmp(val, "7.4") && !strcmp(unit, "s"));
        param_format(&UMOD_PD[UM_RATE], 127, val, &unit);
        ck("RATE 127 reads 0.1 s", !strcmp(val, "0.1") && !strcmp(unit, "s"));
        param_format(&UMOD_PD[UM_RATE], 40, val, &unit);
        printf("umod: RATE 40 reads %s %s\n", val, unit);
        set(0, 0, UMS_SAW, 0, DEST(DEV_FILTER, SVF_FREQ), 63);
        umod_st[0][0].ph = 0;
        run(0, DEV_FILTER, SVF_FREQ, 2000, 0, &mn, &mx, &rs);
        ck("RATE 0: 2000 blocks (1.5 s) of a 10 minute cycle barely move", mx - mn <= 2);
    }
    /* 5 */
    {
        uint32_t steps = 0, big = 0;
        int32_t a = 9999, b = 9999;
        int two = 1;
        set(0, 0, UMS_SQR, 127, DEST(DEV_FILTER, SVF_FREQ), 8);
        run(0, DEV_FILTER, SVF_FREQ, 400, out, &mn, &mx, &rs);
        for (i = 0; i < 400; i++) {
            if (a == 9999 || out[i] == a) a = out[i];
            else if (b == 9999 || out[i] == b) b = out[i];
            else two = 0;
        }
        ck("SQR: two values", two && a != b);
        set(0, 0, UMS_SH, 127, DEST(DEV_FILTER, SVF_FREQ), 20);
        run(0, DEV_FILTER, SVF_FREQ, 1380, out, &mn, &mx, &rs);
        for (i = 1; i < 1380; i++)
            steps += out[i] != out[i - 1];
        printf("umod: S&H over 10 cycles: %u steps, %d .. %d\n", steps, mn, mx);
        ck("S&H: a step a cycle at most (10 cycles), different values", steps >= 5 && steps <= 11 && mx > mn);
        set(0, 0, UMS_DRIFT, 127, DEST(DEV_FILTER, SVF_FREQ), 20);
        run(0, DEV_FILTER, SVF_FREQ, 1380, out, &mn, &mx, &rs);
        for (i = 1; i < 1380; i++)
            big += abs(out[i] - out[i - 1]) > 2;
        printf("umod: DRIFT over 10 cycles: %d .. %d, %u jumps over 2\n", mn, mx, big);
        ck("DRIFT: moves, glides (no jump over 2 a block)", mx > mn && big == 0);
        set(0, 0, UMS_SAW, 127, DEST(DEV_FILTER, SVF_FREQ), 8);
        umod_st[0][0].ph = 0xC0000000u;
        run(0, DEV_FILTER, SVF_FREQ, 2, out, &mn, &mx, &rs);
        set(0, 0, UMS_SAW, 127, DEST(DEV_FILTER, SVF_FREQ), -8);
        umod_st[0][0].ph = 0xC0000000u;
        run(0, DEV_FILTER, SVF_FREQ, 2, out + 2, &mn, &mx, &rs);
        ck("negative DEPTH: upside down (a rising saw falls)", out[1] > 60 && out[3] < 60);
    }
    /* 6 */
    set(0, 0, UMS_SQR, 127, DEST(DEV_FILTER, SVF_FREQ), 8);
    set(0, 1, UMS_SQR, 50, DEST(DEV_FILTER, SVF_FREQ), 8);
    umod_st[0][0].ph = 0, umod_st[0][1].ph = 0;
    run(0, DEV_FILTER, SVF_FREQ, 3, out, &mn, &mx, &rs);
    printf("umod: two SQR MODS on FREQ 60: %d\n", out[0]);
    ck("two MODS on one value: both add, the stored value back", out[0] >= 88 && rs && uchain[0].p[DEV_FILTER][SVF_FREQ] == 60);
    set(0, 1, UMS_SINE, 64, 0, 32);
    /* 7 */
    chain_set_type(0, DEV_FILTER, FILTER_FORMANT);
    set(0, 0, UMS_SINE, 127, DEST(DEV_FILTER, 7), 63);      /* (FORMANT has 6 values: no 8th) */
    run(0, DEV_FILTER, 7, 300, 0, &mn, &mx, &rs);
    ck("a type change: a DEST its new type lacks does nothing", mn == mx && rs);
    printf(bad ? "umod: %d FAILED\n" : "umod: all ok\n", bad);
    return bad ? 1 : 0;
}
