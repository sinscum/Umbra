/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's chain (docs/DESIGN.md §3): each track runs SOURCE -> GRANULAR -> FILTER -> DRIVE -> SPACE. A slot holds a
 * device type, a bypass bit and the device's values (DEV_NP of them, as the type's descriptors say); each type
 * declares a name, a CPU cost and its pages.
 *
 * The types (index 0 the default, the base tree's behaviour):
 *   SOURCE   SYNTH: the track's engine (voice.c track_render)
 *   GRANULAR THRU: nothing yet (Phase 4)
 *   FILTER   THRU: nothing
 *   DRIVE    BASIC: the base tree's DIST, INSERT and SLICER (fx.c track_dist / track_insert, slicer.c)
 *   SPACE    SENDS: the base tree's sends to its chorus, delay and reverb (fx.c mix_part)
 * With every slot at its default type and nothing bypassed the sound is bit for bit the base tree's
 * (tests/regress.c golden renders). Bypass: SOURCE silences the track; GRANULAR and FILTER pass; DRIVE skips its
 * processing (BASIC: DIST and INSERT; the SLICER's step clock runs on without its gate); SPACE sends nothing.
 * Values: per track and device, int16 like the base tree's track values (the pages edit them in place: params.c
 * page_desc, scope SC_DEV); a type change sets its defaults. They are not P_* (the formats and the editor protocol
 * fix those), so no motion records, MIDI LEARN or MOMENTARY for them yet (Phase 5).
 * UMBRA_CHAIN 0 builds none of the audio (the base tree's mix_part). The UI writes the slots (one writer), the audio
 * reads. */
#ifndef UMBRA_CHAIN
#define UMBRA_CHAIN 1
#endif

enum { DEV_SOURCE, DEV_GRANULAR, DEV_FILTER, DEV_DRIVE, DEV_SPACE, DEV_COUNT };
#define DEV_NP 8                                        /* values a device has (two pages of four) */

typedef struct {
    const char *name;           /* on the device page's context line and in the type message, <= 8 */
    uint16_t cost;              /* host instructions per sample per track (tests/cpu_baseline.txt's unit), 0 = free */
    const param_desc_t *pd;     /* its DEV_NP values' descriptors (0: none) */
} dev_type_t;

typedef struct {
    const char *name;           /* the device, plain (DESIGN naming rule) */
    const char *tag;            /* the chain strip's label, 3 characters */
    uint8_t ntypes;
    const dev_type_t *types;
} dev_desc_t;

/* the device types' DSP and descriptors (each its own file, before the table); their buffers' sizes: umbra_mem.h */
#include "umbra_mem.h"
#include "umbra_drive.c"
#include "umbra_filter.c"
#include "umbra_space.c"
#include "umbra_master.c"

static const dev_type_t DEV_T_SOURCE[] = {{"SYNTH", 0, 0}};   /* the engine's cost is its own (ENG_VCOST) */
static const dev_type_t DEV_T_GRANULAR[] = {{"THRU", 0, 0}};
static const dev_type_t DEV_T_FILTER[] = {{"THRU", 0, 0}, FLT_T_SVF, FLT_T_COMB, FLT_T_RES, FLT_T_FORMANT};
static const dev_type_t DEV_T_DRIVE[] = {{"BASIC", 0, 0}, DRV_T_DAMAGE};   /* BASIC: the base tree's, in its budget */
static const dev_type_t DEV_T_SPACE[] = {{"SENDS", 0, 0}};
#define NELEM_T(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
static const dev_desc_t UCH_DESC[DEV_COUNT] = {
    {"SOURCE", "SRC", NELEM_T(DEV_T_SOURCE), DEV_T_SOURCE},
    {"GRANULAR", "GRN", NELEM_T(DEV_T_GRANULAR), DEV_T_GRANULAR},
    {"FILTER", "FLT", NELEM_T(DEV_T_FILTER), DEV_T_FILTER},
    {"DRIVE", "DRV", NELEM_T(DEV_T_DRIVE), DEV_T_DRIVE},
    {"SPACE", "SPC", NELEM_T(DEV_T_SPACE), DEV_T_SPACE},
};
enum { DRIVE_BASIC, DRIVE_DAMAGE };                     /* (DEV_T_DRIVE's order: stored) */

typedef struct {
    uint8_t type[DEV_COUNT];    /* index into UCH_DESC[d].types */
    uint8_t bypass;             /* bit d: device d bypassed */
    uint8_t rsv;
    int16_t p[DEV_COUNT][DEV_NP];   /* each device's values */
} umbra_chain_t;

static volatile umbra_chain_t uchain[NTRK];             /* chain_reset at power-on: defaults, nothing bypassed */

static inline const dev_type_t *dev_type(uint32_t tr, uint32_t d)
{
    return &UCH_DESC[d % DEV_COUNT].types[uchain[tr % NTRK].type[d % DEV_COUNT] % UCH_DESC[d % DEV_COUNT].ntypes];
}

static inline uint32_t chain_off(const track_t *t, uint32_t d)
{
#if UMBRA_CHAIN
    return (uchain[(uint32_t)(t - trk) % NTRK].bypass >> d) & 1u;
#else
    (void)t; (void)d;
    return 0;
#endif
}
static inline uint32_t chain_type(const track_t *t, uint32_t d)   /* the slot's type (UMBRA_CHAIN 0: the default) */
{
#if UMBRA_CHAIN
    return uchain[(uint32_t)(t - trk) % NTRK].type[d];
#else
    (void)t; (void)d;
    return 0;
#endif
}

/* FILTER runs on this track now (its type, not bypassed) */
static inline int chain_filter_on(const track_t *t)
{
    return chain_type(t, DEV_FILTER) != FILTER_THRU && !chain_off(t, DEV_FILTER);
}

/* after SOURCE rendered into b: SOURCE's bypass, then GRANULAR and FILTER */
static inline void chain_pre(const track_t *t, int32_t *b, uint32_t n)
{
    uint32_t i, tr = (uint32_t)(t - trk) % NTRK;
    if (chain_off(t, DEV_SOURCE))
        for (i = 0; i < n; i++)
            b[i] = 0;
    if (chain_filter_on(t)) {
        const int16_t *p = (const int16_t *)uchain[tr].p[DEV_FILTER];
        flt_state_t *s = &flt_st[tr];
        uint32_t ty = chain_type(t, DEV_FILTER);
        if (s->on && s->on != ty + 1u)                  /* (another type than last block: a fresh start) */
            s->on = 0;
        if (song.cpu_q8 > 205u)                         /* RESONATOR: 4 bands over ~80 % CPU, 6 again under ~66 % */
            flt_res_bands = 4;
        else if (song.cpu_q8 < 170u)
            flt_res_bands = FLT_BANDS;
        if (ty == FILTER_SVF)
            flt_svf(s, t, p, b, n);
        else if (ty == FILTER_COMB)
            flt_comb(s, t, p, b, n, flt_comb_buf[tr]);
        else if (ty == FILTER_RES)
            flt_res(s, t, p, b, n);
        else
            flt_formant(s, t, p, b, n);
        s->on = (uint8_t)(ty + 1u);                     /* (which type ran) */
    } else {
        flt_st[tr].on = 0;                              /* (on again later: a fresh start) */
    }
}

/* a device still sounding after the track's voices (fx.c mix_part runs the track on: its tail) */
static inline int chain_ringing(const track_t *t)
{
    return chain_filter_on(t) && flt_ringing(&flt_st[(uint32_t)(t - trk) % NTRK]);
}

/* DRIVE's own types (not BASIC, not bypassed): 1 = it ran on b (fx.c mix_part: then no DIST, INSERT or SLICER gate) */
static inline int chain_drive(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK;
    if (chain_type(t, DEV_DRIVE) == DRIVE_DAMAGE) {
        drv_damage(&drv_st[tr], (const int16_t *)uchain[tr].p[DEV_DRIVE], b, n);
        return 1;
    }
    drv_st[tr].on = 0;                                  /* (DAMAGE again later: a fresh start) */
    return 0;
}

/* the UI side (one writer) */
static void chain_set_bypass(uint32_t tr, uint32_t d, int on)
{
    uint8_t m = (uint8_t)(1u << d);
    if (tr < NTRK && d < DEV_COUNT)
        uchain[tr].bypass = (uint8_t)(on ? uchain[tr].bypass | m : uchain[tr].bypass & ~m);
}
static void dev_defaults(uint32_t tr, uint32_t d)     /* device d's values: its type's defaults (0 without) */
{
    const param_desc_t *pd = dev_type(tr, d)->pd;
    uint32_t i;
    for (i = 0; i < DEV_NP; i++)
        uchain[tr].p[d][i] = pd ? pd[i].def : 0;
}
static void chain_set_type(uint32_t tr, uint32_t d, uint32_t type)   /* (its defaults when it changes) */
{
    if (tr < NTRK && d < DEV_COUNT && type < UCH_DESC[d].ntypes && uchain[tr].type[d] != type) {
        uchain[tr].type[d] = (uint8_t)type;
        dev_defaults(tr, d);
    }
}
static void chain_reset(void)                           /* every track: first types and their defaults, no bypass */
{
    uint32_t i, d;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++) {
            uchain[i].type[d] = 0;
            dev_defaults(i, d);
        }
        uchain[i].bypass = 0;
    }
    usong_reset();                                      /* (SPACE's and MASTER's song values; FREEZE off) */
    usolo = 0;
}

/* the device a page family is (DESIGN §7: EDIT ARP SCL FX GLO open SOURCE GRANULAR FILTER DRIVE SPACE); DEV_COUNT none */
static uint32_t fam_device(uint32_t fam)
{
    switch (fam) {
    case FAM_EDIT: return DEV_SOURCE;
    case FAM_ARP: return DEV_GRANULAR;
    case FAM_SCL: return DEV_FILTER;
    case FAM_FX: return DEV_DRIVE;
    case FAM_GLO: return DEV_SPACE;
    default: return DEV_COUNT;
    }
}

/* the pages that belong to one type of a device (shown only while the selected track's slot has it); pages of a
 * device's family not listed here show whatever the type (SCL and CHORD on FILTER's, SPACE's) */
static const struct { const char *title; uint8_t dev, type; } DEV_PAGES[] = {
    {"FX", DEV_DRIVE, DRIVE_BASIC}, {"SLICER", DEV_DRIVE, DRIVE_BASIC}, {"INSERT", DEV_DRIVE, DRIVE_BASIC},
    {"INSERT 2", DEV_DRIVE, DRIVE_BASIC},
    {"DAMAGE", DEV_DRIVE, DRIVE_DAMAGE}, {"DAMAGE 2", DEV_DRIVE, DRIVE_DAMAGE},
    {"THRU", DEV_FILTER, FILTER_THRU}, {"SVF", DEV_FILTER, FILTER_SVF}, {"SVF 2", DEV_FILTER, FILTER_SVF},
    {"COMB", DEV_FILTER, FILTER_COMB}, {"COMB 2", DEV_FILTER, FILTER_COMB},
    {"RESONATOR", DEV_FILTER, FILTER_RES}, {"RESONATOR 2", DEV_FILTER, FILTER_RES},
    {"FORMANT", DEV_FILTER, FILTER_FORMANT}, {"FORMANT 2", DEV_FILTER, FILTER_FORMANT},
};
static int dev_page_visible(const page_t *pg)
{
    uint32_t d = fam_device(pg->fam), k;
    for (k = 0; d < DEV_COUNT && k < sizeof DEV_PAGES / sizeof DEV_PAGES[0]; k++)
        if (DEV_PAGES[k].dev == d && str_eq(DEV_PAGES[k].title, pg->title))
            return uchain[song.sel % NTRK].type[d] == DEV_PAGES[k].type;
    return 1;
}

/* params.c page_desc, scope SC_DEV: the value KNOB slot edits on the selected track's device (the page's family);
 * id the value's index */
static const param_desc_t *dev_page_desc(const page_t *pg, uint32_t id, int16_t **valp)
{
    uint32_t d = fam_device(pg->fam), tr = song.sel % NTRK;
    const param_desc_t *pd = d < DEV_COUNT ? dev_type(tr, d)->pd : 0;
    if (!pd || id >= DEV_NP) {
        *valp = 0;
        return 0;
    }
    *valp = (int16_t *)&uchain[tr].p[d][id];
    return &pd[id];
}
