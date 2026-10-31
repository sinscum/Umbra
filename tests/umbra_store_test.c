/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* UMBRA: the chains saved beside the projects (firmware/src/umbra_store.c) against NOR flash. Harness: persistence_test.c's. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t irq_start_race, irq_races;
static void fm1_irq_off(void)
{
    if (irq_start_race) {                         /* audio serviced PLAY immediately before main masks it */
        irq_start_race = 0;
        events_block(CTL);
        irq_races++;
    }
}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define UMBRA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1;
static uint32_t erases, as_erases;               /* (as_erases: the autosave's two sectors, 0xE5000 / 0xE6000) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    as_erases += off >= 0xE5000u && off < 0xE7000u;
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

enum { ED_UI_STATE = 34, ED_UI_SET, ED_UI_PALETTES, ED_FAV_GET, ED_FAV_SET };
static uint8_t ed_rep[64];                        /* the reply's bytes (MENU_SET) */
static uint32_t ed_repn;
static void ed_b(uint32_t n) { if (ed_repn < sizeof ed_rep) ed_rep[ed_repn++] = (uint8_t)(n & 127u); }
static void ed_v(int32_t n) { ed_b((uint32_t)(n + 8192)); ed_b((uint32_t)(n + 8192) >> 7); }
static int32_t ed_rv(const uint8_t *a) { return (int32_t)(a[0] | (uint32_t)a[1] << 7) - 8192; }

static int check(const char *what, int ok)
{
    printf("store: %-84s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(uch_slot, 0, sizeof uch_slot);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    chain_reset();
    fm1_ms = 0;
    transport_req = 0;
    flash_ok = 1;
    erase_error = 0;
    fail_after = -1;
}
static void power_cycle(void)                    /* RAM lost: the slots and their chains come back from flash */
{
    uint32_t i;
    memset(proj_slot, 0, sizeof proj_slot);
    chain_reset();
    for (i = 0; i < 4u; i++)
        proj_fetch(i);
    uch_boot();
}
static int chain_is(uint32_t t1, uint32_t t2)    /* bypass masks of tracks 1 and 2, the others none */
{
    return uchain[0].bypass == t1 && uchain[1].bypass == t2 && !uchain[2].bypass && !uchain[3].bypass;
}

int main(void)
{
    int bad = 0;
    const uint32_t A = 1u << DEV_FILTER, B = (1u << DEV_DRIVE) | (1u << DEV_SPACE);
    reset();
    settings_init();

    /* 1. a save and a load */
    chain_set_bypass(0, DEV_FILTER, 1);
    chain_set_bypass(1, DEV_DRIVE, 1);
    chain_set_bypass(1, DEV_SPACE, 1);
    bad += check("a project saved (slot A)", project_save(0) == 0 && !strcmp(ui.msg, "SAVED"));
    chain_reset();
    trk[0].step[0].note[0] = 1;
    project_load(0);
    bad += check("loaded: its chain back (T1 FILTER, T2 DRIVE SPACE bypassed)", chain_is(A, B));

    /* 2. power-off and on */
    power_cycle();
    project_load(0);
    bad += check("after a power cycle (from flash): the same chain", chain_is(A, B));

    /* 3. a project with no chain record (the base tree's, or UMBRA's before the chain) */
    chain_reset();
    chain_set_bypass(3, DEV_SOURCE, 1);
    project_capture(&proj_scratch);
    bad += check("(a base-tree project written to slot B, its record only)",
                 proj_pack(&proj_wire, &proj_scratch) && st_save(OBJ_PROJECT0 + 1, &proj_wire, sizeof proj_wire) == 0);
    power_cycle();
    chain_set_bypass(2, DEV_DRIVE, 1);
    project_load(1);
    bad += check("a project with no chain record loads with the default chain (nothing bypassed)", chain_is(0, 0) &&
                 !strcmp(ui.msg, "LOADED"));

    /* 4. a slot saved by UMBRA, then re-saved by the base tree: its old chain record is not this project's */
    trk[0].step[0].note[0] = 77;                     /* (another project) */
    project_capture(&proj_scratch);
    proj_pack(&proj_wire, &proj_scratch);
    st_save(OBJ_PROJECT0, &proj_wire, sizeof proj_wire);
    power_cycle();
    project_load(0);
    bad += check("slot A re-saved by the base tree: the default chain, not the old record's", chain_is(0, 0));

    /* 5. an erase, then a new save in the slot */
    chain_set_bypass(0, DEV_GRANULAR, 1);
    project_save(2);
    bad += check("slot C erased", project_erase(2) == 0 && !project_used(2));
    chain_reset();
    chain_set_bypass(1, DEV_SOURCE, 1);
    project_save(2);
    chain_reset();
    power_cycle();
    project_load(2);
    bad += check("a new save in the erased slot: its own chain (T2 SOURCE bypassed)", chain_is(0, 1u << DEV_SOURCE));

    /* 6. the autosave keeps the chain; RESTORE LAST brings both back */
    chain_reset();
    chain_set_bypass(0, DEV_SPACE, 1);
    {
        uint32_t sig = autosave_sig();
        chain_set_bypass(0, DEV_SPACE, 0);
        bad += check("a bypass is a change for the autosave (its signature)", autosave_sig() != sig);
        chain_set_bypass(0, DEV_SPACE, 1);
    }
    project_capture(&proj_scratch);
    proj_pack(&proj_wire, &proj_scratch);
    st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire);
    uch_autosaved(&proj_wire, sizeof proj_wire);
    power_cycle();
    bad += check("power-on with RESTORE LAST: the autosave and its chain (T1 SPACE bypassed)",
                 autosave_boot(1) == 1 && chain_is(1u << DEV_SPACE, 0));

    /* 7. a chain write cut by a power cut: the project loads, with the default chain (never another one's) */
    reset();
    settings_init();
    chain_set_bypass(0, DEV_DRIVE, 1);
    project_save(3);
    chain_set_bypass(1, DEV_FILTER, 1);
    {
        uint32_t i;
        fail_after = 0;                              /* (cut: its chain's record never committed) */
        uch_saved(3);
        fail_after = -1;
        power_cycle();
        project_load(3);
        bad += check("a torn chain write, the project unchanged: the last chain committed for it (T1 DRIVE)",
                     chain_is(1u << DEV_DRIVE, 0));
        chain_set_bypass(1, DEV_FILTER, 1);
        trk[1].step[3].note[0] = 50;                 /* (the project changed too) */
        project_capture(&proj_scratch);
        proj_pack(&proj_wire, &proj_scratch);
        fail_after = -1;
        st_save(OBJ_PROJECT0 + 3, &proj_wire, sizeof proj_wire);   /* (the project's record written) */
        memcpy(&proj_slot[3], &proj_wire, sizeof proj_wire);
        fail_after = 0;
        uch_saved(3);                                /* (its chain's: cut) */
        fail_after = -1;
        power_cycle();
        project_load(3);
        bad += check("a torn chain write, the project changed: it loads with the default chain", chain_is(0, 0) &&
                     !strcmp(ui.msg, "LOADED"));
        (void)i;
    }

    /* 8. the record */
    bad += check("the chain record: 4 tracks x 6 bytes + 12, in one object",
                 sizeof(uch_store_t) == 12u + 4u * 6u && sizeof(uch_store_t) <= ST_PAYLOAD_MAX);
    printf(bad ? "store: %d FAILED\n" : "store: all ok\n", bad);
    return bad ? 1 : 0;
}
