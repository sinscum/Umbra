/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's chain (docs/DESIGN.md §3): each track runs SOURCE -> GRANULAR -> FILTER -> DRIVE -> SPACE. A slot holds a
 * device type and a bypass bit; each type declares a name and a CPU cost.
 *
 * Phase 1 (docs/ROADMAP.md): the base tree's parts take the slots that fit them, and the rest pass through:
 *   SOURCE   SYNTH: the track's engine (voice.c track_render)
 *   GRANULAR THRU: nothing yet (Phase 4)
 *   FILTER   THRU: nothing yet (Phase 2)
 *   DRIVE    BASIC: the base tree's DIST, INSERT and SLICER (fx.c track_dist / track_insert, slicer.c)
 *   SPACE    SENDS: the base tree's sends to its chorus, delay and reverb (fx.c mix_part)
 * With nothing bypassed the sound is bit for bit the base tree's (tests/regress.c golden renders).
 * Bypass: SOURCE silences the track; GRANULAR and FILTER pass (they already do); DRIVE skips DIST and INSERT and
 * keeps the SLICER's step clock running without its gate; SPACE sends nothing (the dry signal only).
 * UMBRA_CHAIN 0 builds none of it (the base tree's mix_part). The UI writes the slots (one writer), the audio reads. */
#ifndef UMBRA_CHAIN
#define UMBRA_CHAIN 1
#endif

enum { DEV_SOURCE, DEV_GRANULAR, DEV_FILTER, DEV_DRIVE, DEV_SPACE, DEV_COUNT };

typedef struct {
    const char *name;           /* on the device page's context line, <= 8 */
    uint16_t cost;              /* host instructions per sample per track (tests/cpu_baseline.txt's unit), 0 = free */
} dev_type_t;

typedef struct {
    const char *name;           /* the device, plain (DESIGN naming rule) */
    const char *tag;            /* the chain strip's label, 3 characters */
    uint8_t ntypes;
    const dev_type_t *types;
} dev_desc_t;

static const dev_type_t DEV_T_SOURCE[] = {{"SYNTH", 0}};      /* the engine's cost is its own (ENG_VCOST) */
static const dev_type_t DEV_T_GRANULAR[] = {{"THRU", 0}};
static const dev_type_t DEV_T_FILTER[] = {{"THRU", 0}};
static const dev_type_t DEV_T_DRIVE[] = {{"BASIC", 0}};       /* the base tree's, already in its budget */
static const dev_type_t DEV_T_SPACE[] = {{"SENDS", 0}};
static const dev_desc_t UCH_DESC[DEV_COUNT] = {
    {"SOURCE", "SRC", 1, DEV_T_SOURCE},
    {"GRANULAR", "GRN", 1, DEV_T_GRANULAR},
    {"FILTER", "FLT", 1, DEV_T_FILTER},
    {"DRIVE", "DRV", 1, DEV_T_DRIVE},
    {"SPACE", "SPC", 1, DEV_T_SPACE},
};

typedef struct {
    uint8_t type[DEV_COUNT];    /* index into UCH_DESC[d].types */
    uint8_t bypass;             /* bit d: device d bypassed */
} umbra_chain_t;

static volatile umbra_chain_t uchain[NTRK];             /* zero = every slot its first type, nothing bypassed */

static inline uint32_t chain_off(const track_t *t, uint32_t d)
{
#if UMBRA_CHAIN
    return (uchain[(uint32_t)(t - trk) % NTRK].bypass >> d) & 1u;
#else
    (void)t; (void)d;
    return 0;
#endif
}

/* after SOURCE rendered into b: SOURCE's bypass, then GRANULAR and FILTER (pass-through in Phase 1) */
static inline void chain_pre(const track_t *t, int32_t *b, uint32_t n)
{
    uint32_t i;
    if (chain_off(t, DEV_SOURCE))
        for (i = 0; i < n; i++)
            b[i] = 0;
}

/* the UI side (one writer) */
static void chain_set_bypass(uint32_t tr, uint32_t d, int on)
{
    uint8_t m = (uint8_t)(1u << d);
    if (tr < NTRK && d < DEV_COUNT)
        uchain[tr].bypass = (uint8_t)(on ? uchain[tr].bypass | m : uchain[tr].bypass & ~m);
}
static void chain_set_type(uint32_t tr, uint32_t d, uint32_t type)
{
    if (tr < NTRK && d < DEV_COUNT && type < UCH_DESC[d].ntypes)
        uchain[tr].type[d] = (uint8_t)type;
}
static void chain_reset(void)                           /* every track: first types, nothing bypassed */
{
    uint32_t i, d;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++)
            uchain[i].type[d] = 0;
        uchain[i].bypass = 0;
    }
}
