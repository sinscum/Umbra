/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of UMBRA's chain (firmware/src/umbra_chain.c, the hooks in fx.c mix_part / mix_spread), on the real
 * sources (tests/regress.c's golden renders cover "nothing bypassed = the base tree", with UMBRA_CHAIN 1 and 0):
 * 1. SOURCE bypassed: the track is silent (no dry, no sends).
 * 2. DRIVE bypassed with a loud DIST and an INSERT: the same as DIST 0 and INSERT OFF, POLY and SPREAD.
 * 3. SPACE bypassed: the same as CHOR, DLY and REV at 0, POLY and SPREAD.
 * 4. a bypass on another track changes nothing on this one; clearing it restores the sound bit for bit.
 * 5. the descriptors: 5 devices, tags of 3, names that fit, every slot's types, types out of range refused. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("chain: %-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
typedef struct { uint64_t hash; int32_t peak; } ph_t;

static void fresh(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    memset(ins, 0, sizeof ins);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    song.playing = 0;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    memset(&motion, 0, sizeof motion);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], 0, 0);
    song.sel = 0;
    chain_reset();
}

/* what a phrase sets on track 1 before it plays */
static struct { int dist, ins, sends, sprd, byp, byp_trk, damage, fold; } ph;

/* a chord phrase on track 1 (ANALOG, POLY) in a fork()ed child: its output's hash and peak */
static ph_t phrase(void)
{
    int fd[2];
    ph_t r = {0, 0};
    pid_t pid;
    if (pipe(fd))
        return r;
    fflush(stdout);
    if (!(pid = fork())) {
        track_t *t = &trk[0];
        uint64_t h = 0xCBF29CE484222325ull;
        int32_t pk = 0;
        uint32_t blk, i, k;
        fresh();
        t->p[P_VOICE] = V_POLY;
        t->p[P_SPRD] = (int16_t)ph.sprd;
        t->p[P_DIST] = (int16_t)ph.dist;
        if (ph.ins) {
            t->p[P_ITYPE] = 1;
            t->p[P_IA] = t->p[P_IB] = t->p[P_IC] = 64;
            t->p[P_IMIX] = 127;
        }
        if (ph.sends >= 0)
            t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = (int16_t)ph.sends;
        for (k = 0; k < DEV_COUNT; k++)
            if ((ph.byp >> k) & 1u)
                chain_set_bypass((uint32_t)ph.byp_trk, k, 1);
        if (ph.damage) {                                 /* DRIVE DAMAGE on track 1 (umbra_drive.c) */
            chain_set_type(0, DEV_DRIVE, DRIVE_DAMAGE);
            uchain[0].p[DEV_DRIVE][DRV_FOLD] = (int16_t)ph.fold;
        }
        trk_note_on(t, 48, 100);
        trk_note_on(t, 55, 90);
        trk_note_on(t, 64, 120);
        for (blk = 0; blk < 2u * FS / CTL; blk++) {
            if (blk == FS / CTL) {
                trk_note_off(t, 48);
                trk_note_off(t, 55);
                trk_note_off(t, 64);
            }
            mix_block(out_buf, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
                if (a > pk)
                    pk = a;
                for (k = 0; k < 4u; k++) {
                    h ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
                    h *= 0x100000001B3ull;
                }
            }
        }
        r.hash = h;
        r.peak = pk;
        if (write(fd[1], &r, sizeof r) != (ssize_t)sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != (ssize_t)sizeof r)
        r.hash = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r;
}

static int same(ph_t a, ph_t b) { return a.hash == b.hash && a.peak == b.peak; }
static void base(void) { memset(&ph, 0, sizeof ph); ph.sends = -1; }

int main(void)
{
    ph_t plain, off, x, y;
    uint32_t d, k;
    int s;

    base();
    plain = phrase();
    ck("the phrase sounds (track 1, ANALOG, three notes)", plain.peak > 1000);

    /* 1 */
    base(); ph.byp = 1u << DEV_SOURCE;
    off = phrase();
    ck("SOURCE bypassed: silent, dry and sends", off.peak == 0);

    /* 2, 3: POLY and SPREAD */
    for (s = 0; s < 2; s++) {
        const char *how = s ? " (SPREAD)" : " (POLY)";
        char what[160];
        base(); ph.sprd = s ? 100 : 0; ph.dist = 100; ph.ins = 1;
        x = phrase();
        ph.byp = 1u << DEV_DRIVE;
        y = phrase();
        base(); ph.sprd = s ? 100 : 0;
        off = phrase();
        snprintf(what, sizeof what, "DRIVE bypassed with DIST and an INSERT = DIST 0, INSERT OFF%s", how);
        ck(what, same(y, off) && !same(x, y));

        base(); ph.sprd = s ? 100 : 0; ph.sends = 100;
        x = phrase();
        ph.byp = 1u << DEV_SPACE;
        y = phrase();
        base(); ph.sprd = s ? 100 : 0; ph.sends = 0;
        off = phrase();
        snprintf(what, sizeof what, "SPACE bypassed with full sends = no sends%s", how);
        ck(what, same(y, off) && !same(x, y));
    }

    /* 4 */
    base(); ph.byp = (1u << DEV_SOURCE) | (1u << DEV_DRIVE) | (1u << DEV_SPACE); ph.byp_trk = 2;
    ck("bypasses on track 3 leave track 1 bit for bit", same(phrase(), plain));
    base(); ph.byp = 1u << DEV_GRANULAR | 1u << DEV_FILTER;
    ck("GRANULAR and FILTER bypassed: bit for bit (pass-through in Phase 1)", same(phrase(), plain));

    /* 6. DRIVE DAMAGE (Phase 2): at its defaults the base tree's sound; FOLD changes it; bypassed: as DIST 0 */
    for (s = 0; s < 2; s++) {
        char what[160];
        const char *how = s ? " (SPREAD)" : " (POLY)";
        base(); ph.sprd = s ? 100 : 0;
        off = phrase();
        ph.damage = 1;
        x = phrase();
        snprintf(what, sizeof what, "DRIVE DAMAGE at its defaults = the base tree (DIST 0) bit for bit%s", how);
        ck(what, same(x, off));
        ph.fold = 100;
        y = phrase();
        ph.byp = 1u << DEV_DRIVE;
        x = phrase();
        snprintf(what, sizeof what, "DAMAGE FOLD 100 changes it, bounded; bypassed: the base tree again%s", how);
        ck(what, !same(y, off) && y.peak <= 32767 * 2 && same(x, off));
    }

    /* 5 */
    {
        int ok = 1;
        for (d = 0; d < DEV_COUNT; d++) {
            ok &= strlen(UCH_DESC[d].tag) == 3u && strlen(UCH_DESC[d].name) <= 8u && UCH_DESC[d].ntypes >= 1u;
            for (k = 0; k < UCH_DESC[d].ntypes; k++)
                ok &= UCH_DESC[d].types[k].name && strlen(UCH_DESC[d].types[k].name) <= 8u;
        }
        for (d = 0; d < DEV_COUNT; d++)                 /* (a type with values: each default within its range) */
            for (k = 0; k < UCH_DESC[d].ntypes; k++) {
                const param_desc_t *pd = UCH_DESC[d].types[k].pd;
                uint32_t j;
                for (j = 0; pd && j < DEV_NP; j++)
                    ok &= pd[j].label && pd[j].min <= pd[j].def && pd[j].def <= pd[j].max && strlen(pd[j].label) <= 4u;
            }
        ck("descriptors: 5 devices, 3-letter tags, names <= 8, each with a type; values' defaults in range, labels <= 4",
           ok);
        chain_reset();
        chain_set_type(0, DEV_FILTER, 200);
        chain_set_type(9, DEV_FILTER, 0);
        chain_set_bypass(0, 9, 1);
        ck("a type out of range, a track or device out of range: refused", uchain[0].type[DEV_FILTER] == 0 &&
                                                                        uchain[0].bypass == 0);
        chain_set_bypass(1, DEV_SPACE, 1);
        chain_set_bypass(1, DEV_SPACE, 0);
        ck("bypass on, then off: cleared", uchain[1].bypass == 0);
    }
    printf(bad ? "chain: %d FAILED\n" : "chain: all ok\n", bad);
    return bad ? 1 : 0;
}
