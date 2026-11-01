/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA recipes (docs/DESIGN.md §6): ready-made setups for the browser emulator's demo list, one call each, on the
 * firmware's own loaders (ui.c set_engine_of / apply_preset_to / load_pat16, umbra_chain.c chain_set_type) as
 * power-on (main.c umbra_init) uses them. Emulator-only for now (web/emu/umbra_web.c web_demo, tests/umbra_golden.c);
 * factory projects take them over later. A recipe resets the chain, every track's steps and the tempo, then sets
 * the tracks it uses; the others keep their sounds, silent (no steps).
 *   0 FOUR ENGINES    the power-on sounds, a pattern each
 *   1 SUNKEN PULSE    a saw bass through DRIVE DAMAGE and a keytracked SVF; the kit rings a RESONATOR in fifths
 *   2 DISTANT CHOIR   wind through a wandering FORMANT into a long HALL; a low pad under a dark SVF
 *   3 FROZEN HALL     the choir and the pad into the longest HALL, 40 ms before it, echoes with WOW: turn SPACE 3's
 *                     FREEZE (KNOB 4) on, stop, and the hall hangs (tests/umbra_golden.c freezes it at 4 s)
 *   4 DEAD BELLS      an FM6 bell into a slow cloud of long grains (GRANULAR GRAINS), a RESONATOR in A minor, a
 *                     long HALL (GLO + G5 freezes it)
 *   5 WORN CATHEDRAL  an FM6 pad; track 2's TAPE records the mix (LO) until full, then its take hangs as a suspended
 *                     cloud of grains, pitches at random on the scale (C minor), in a long HALL
 *   6 SHADOW          an ANALOG pluck on track 1; track 4 a RETURN of it: grains 60 % backwards through a COMB, a ghost
 *                     behind it
 *   7 LOW TIDE        an FM6 pad through a resonant SVF; MOD 1 sweeps its FREQ (SINE, ~5 s), MOD 2 drifts its RES */
#define UMBRA_NRECIPES 8u
static const char *const RECIPE_NAME[UMBRA_NRECIPES] = {"FOUR ENGINES", "SUNKEN PULSE", "DISTANT CHOIR", "FROZEN HALL",
                                                        "DEAD BELLS", "WORN CATHEDRAL", "SHADOW", "LOW TIDE"};

static uint32_t recipe_engine(const char *name)      /* an engine by its name (ENGINES[]), 0 if none */
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (eng_ok(e) && !strcmp(ENGINES[e]->name, name))
            return e;
    return 0;
}
/* track tr: engine en, its preset by name (the first if none), pattern pat (PATTERNS[pat - 1], 0: none) */
static void recipe_track(uint32_t tr, const char *en, const char *pn, uint32_t pat)
{
    track_t *t = &trk[tr % NTRK];
    const engine_t *e;
    uint32_t i;
    set_engine_of(t, recipe_engine(en));
    e = ENGINES[t->eng_req % NENGINES];
    for (i = 0; i < e->npresets; i++)
        if (!strcmp(e->presets[i].name, pn)) {
            apply_preset_to(t, i);
            break;
        }
    if (pat)
        load_pat16(t, PATTERNS[pat - 1u].note, PATTERNS[pat - 1u].flags);
}
static void recipe_dev(uint32_t tr, uint32_t d, uint32_t type, const int8_t *v)   /* v: DEV_NP values, -128 kept */
{
    uint32_t i;
    chain_set_type(tr, d, type);
    for (i = 0; i < DEV_NP; i++)
        if (v[i] != -128)
            uchain[tr].p[d][i] = v[i];
}

#define K_ -128
static void umbra_recipe(uint32_t n)
{
    static const uint8_t PAT0[NTRK] = {8, 5, 7, 12};             /* SUB, PAD, BELL, BEAT */
    static const uint8_t ZERO[16];
    uint32_t i;
    chain_reset();
    for (i = 0; i < NTRK; i++)
        load_pat16(&trk[i], ZERO, ZERO);
    song.g[G_BPM] = GP[G_BPM].def;
    switch (n) {
    case 1: {   /* SUNKEN PULSE */
        static const int8_t DMG[DEV_NP] = {50, 60, 40, 0, 10, -20, 30, K_};   /* DRV FOLD CRSH DCM NOIS TILT COMP */
        static const int8_t SVF[DEV_NP] = {55, 70, 0, 127, 127, 20, K_, K_};  /* FREQ RES MRPH MIX KTRK WNDR */
        static const int8_t RSN[DEV_NP] = {36, 100, 50, 90, TUNE_FIFTHS, 30, 20, K_};
        song.g[G_BPM] = 84;
        recipe_track(0, "ANALOG", "SAW LEAD", 2);                  /* OFFBEAT */
        recipe_dev(0, DEV_DRIVE, DRIVE_DAMAGE, DMG);
        recipe_dev(0, DEV_FILTER, FILTER_SVF, SVF);
        trk[3].p[P_REV] = 60;
        load_pat16(&trk[3], PATTERNS[11].note, PATTERNS[11].flags);   /* the power-on kit, BEAT */
        recipe_dev(3, DEV_FILTER, FILTER_RES, RSN);
        break;
    }
    case 2: {   /* DISTANT CHOIR */
        static const int8_t FMT[DEV_NP] = {-10, 100, 30, 127, K_, 127, K_, K_};   /* SHFT RES VOWL MIX KTRK WNDR */
        static const int8_t SVF[DEV_NP] = {30, 40, 0, 127, 0, 40, K_, K_};
        song.g[G_BPM] = 60;
        song.g[G_RTYPE] = 2;                                        /* HALL */
        song.g[G_RSIZE] = 120;
        recipe_track(0, "NOISE", "WIND", 5);                       /* PAD */
        trk[0].p[P_REV] = 110;
        recipe_dev(0, DEV_FILTER, FILTER_FORMANT, FMT);
        load_pat16(&trk[1], PATTERNS[7].note, PATTERNS[7].flags);   /* the power-on pad, SUB */
        trk[1].p[P_REV] = 90;
        recipe_dev(1, DEV_FILTER, FILTER_SVF, SVF);
        break;
    }
    case 3: {   /* FROZEN HALL */
        static const int8_t FMT[DEV_NP] = {-20, 90, 60, 127, K_, 100, K_, K_};
        static const int8_t SVF[DEV_NP] = {25, 30, 0, 127, 0, 60, K_, K_};
        song.g[G_BPM] = 56;
        song.g[G_RTYPE] = 2;                                        /* HALL, the longest, little damping */
        song.g[G_RSIZE] = 127;
        song.g[G_RDAMP] = 20;
        song.g[G_DTIME] = 4;                                        /* (1/4D) echoes, wavering */
        song.g[G_DFDBK] = 70;
        usong[US_PRE] = 40;
        usong[US_WOW] = 50;
        recipe_track(0, "NOISE", "WIND", 5);
        trk[0].p[P_REV] = 120;
        trk[0].p[P_DLY] = 40;
        recipe_dev(0, DEV_FILTER, FILTER_FORMANT, FMT);
        load_pat16(&trk[1], PATTERNS[7].note, PATTERNS[7].flags);   /* the power-on pad, SUB */
        trk[1].p[P_REV] = 110;
        recipe_dev(1, DEV_FILTER, FILTER_SVF, SVF);
        break;
    }
    case 4: {   /* DEAD BELLS */
        static const int8_t GRN[DEV_NP] = {110, 90, -12, 30, 20, 40, CONT_HANN, 100, 15, 0, 0, 30};
        static const int8_t RSN[DEV_NP] = {45, 115, 64, 50, TUNE_MINOR, 20, 30, K_};
        song.g[G_BPM] = 66;
        song.g[G_RTYPE] = 2;
        song.g[G_RSIZE] = 115;
        recipe_track(0, "FM6", "BELL", 7);                         /* BELL: sparse */
        trk[0].p[P_LEVEL] = 127;                                    /* (the bell is quiet, the RESONATOR takes more) */
        trk[0].p[P_REV] = 100;
        trk[0].p[P_DLY] = 30;
        recipe_dev(0, DEV_GRANULAR, GRAN_GRAINS, GRN);
        recipe_dev(0, DEV_FILTER, FILTER_RES, RSN);
        break;
    }
    case 5: {   /* WORN CATHEDRAL */
        static const int8_t TAP[DEV_NP] = {127, 0, 0, 127, K_, K_, TMODE_LOOP, TDIR_FWD, TFROM_MIX, TQ_LO, TREC_ON, K_};
        static const int8_t GRN[DEV_NP] = {100, 70, 0, 60, 40, 0, CONT_HANN, 127, 20, 7, 1, 20};   /* SPEED 0: hangs */
        song.g[G_BPM] = 52;
        song.g[G_RTYPE] = 2;
        song.g[G_RSIZE] = 120;
        recipe_track(0, "FM6", "PAD", 5);                          /* PAD: long notes */
        trk[0].p[P_LEVEL] = 127;                                    /* (a soft pad: the take is the mix, as loud) */
        trk[0].p[P_REV] = 70;
        trk[0].p[P_SCALE] = 2;                                      /* (C minor: the grains' scale) */
        trk[1].p[P_SCALE] = 2;
        trk[1].p[P_ROOT] = 0;
        trk[1].p[P_LEVEL] = 127;
        trk[1].p[P_REV] = 100;
        recipe_dev(1, DEV_SOURCE, SRC_TAPE, TAP);
        recipe_dev(1, DEV_GRANULAR, GRAN_GRAINS, GRN);
        break;
    }
    case 6: {   /* SHADOW */
        static const int8_t RET[DEV_NP] = {1, 112, K_, K_, K_, K_, K_, K_};
        static const int8_t GRN[DEV_NP] = {70, 70, 0, 40, 30, 64, CONT_HANN, 127, 76, 0, 0, 20};   /* REVERSE 60 % */
        static const int8_t CMB[DEV_NP] = {57, 85, 50, 90, K_, 40, K_, K_};
        song.g[G_BPM] = 84;
        recipe_track(0, "ANALOG", "PLUCK", 3);                     /* MELODY */
        trk[0].p[P_REV] = 30;
        recipe_dev(3, DEV_SOURCE, SRC_RETURN, RET);
        recipe_dev(3, DEV_GRANULAR, GRAN_GRAINS, GRN);
        recipe_dev(3, DEV_FILTER, FILTER_COMB, CMB);
        trk[3].p[P_REV] = 90;
        trk[3].p[P_PAN] = 40;
        break;
    }
    case 7: {   /* LOW TIDE */
        static const int8_t SVF[DEV_NP] = {50, 90, 0, 127, 0, 0, K_, K_};   /* FREQ RES MORPH MIX KTRK WNDR */
        song.g[G_BPM] = 60;
        song.g[G_RTYPE] = 2;
        song.g[G_RSIZE] = 110;
        recipe_track(0, "FM6", "PAD", 5);                          /* PAD: long notes */
        trk[0].p[P_LEVEL] = 120;
        trk[0].p[P_REV] = 80;
        recipe_dev(0, DEV_FILTER, FILTER_SVF, SVF);
        umod[0][0][UM_SHAPE] = UMS_SINE;                            /* MOD 1: FREQ up and down, ~5 s */
        umod[0][0][UM_RATE] = 70;
        umod[0][0][UM_DEST] = DEV_FILTER * DEV_NP + SVF_FREQ + 1;
        umod[0][0][UM_DEPTH] = 20;
        umod[0][1][UM_SHAPE] = UMS_DRIFT;                           /* MOD 2: RES wandering, ~20 s */
        umod[0][1][UM_RATE] = 45;
        umod[0][1][UM_DEST] = DEV_FILTER * DEV_NP + SVF_RES + 1;
        umod[0][1][UM_DEPTH] = 12;
        break;
    }
    default:    /* FOUR ENGINES */
        for (i = 0; i < NTRK; i++)
            load_pat16(&trk[i], PATTERNS[PAT0[i] - 1u].note, PATTERNS[PAT0[i] - 1u].flags);
        break;
    }
}
#undef K_
