/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's saved chain ("UMB1", version 5): each project slot's tracks' chains (umbra_chain.c: types, bypasses, values)
 * and the song's SPACE / MASTER values (umbra_space.c usong: WOW SHIMMER PRE-DLY, DJ FILTER GLUE; FREEZE never),
 * beside the project in a storage object of its own (storage.c OBJ_UCHAIN0.., A/B at 0xE8000..: the BSP's FL_XDATA1,
 * a firmware's own data), and the autosave's (OBJ_UCHAIN_AUTO). The project record stays the base tree's (a full
 * flash sector: no room), so a project saved by the base tree, or by UMBRA before the chain, still loads.
 *
 * A chain record names the project record it was saved with (proj_crc: the record's CRC, as packed): a project
 * loaded whose record is not that one (none saved with it, the slot re-saved by the base tree since, erased, a torn
 * write) gets the default chain (every slot its first type, nothing bypassed). Included by project.c, after its
 * staging (proj_wire, proj_slot). Main loop only. */
#define UCH_MAGIC 0x31424D55u                  /* "UMB1" */
#define UCH_VER 5u                             /* 5 (Phase 5a): + each track's two MODS. 4 (Phase 4): 12 values a device. 3 (Phase 3): + the song's 8 values.
                                                * 2 (Phase 2): + each device's 8 values. 1 (Phase 1): types,
                                                * bypasses. Older ones load: what they lack, the defaults */
#define UCH_NP 12u                             /* values a device as stored (DEV_NP) */
typedef struct {                               /* a track's chain as stored (fixed: not umbra_chain_t, which may grow) */
    uint8_t type[5], bypass, rsv[2];
    int16_t p[5][UCH_NP];
} uch_trk_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, ntrk;
    uint32_t proj_crc;                         /* the project record's (uch_crc), 0 = none */
    uch_trk_t c[NTRK];
    int16_t song[8];                           /* usong (US_FRZ stored as 0) */
    int8_t mod[NTRK][2][4];                    /* v5: umod (umbra_mod.c: SHAPE RATE DEST DEPTH) */
} uch_store_t;
#define UCH_MOD_DEF 0x80                       /* a MOD value "the default" (-128: no MOD value is; RATE 127 is 0x7F) */
#define UCH_V4_BYTES (12u + NTRK * 128u + 16u)  /* version 4, as Phase 4 wrote it: v5 without the MODS */
typedef struct {                               /* a track as versions 2 and 3 stored it: 8 values a device */
    uint8_t type[5], bypass, rsv[2];
    int16_t p[5][8];
} uch_trk8_t;
typedef struct {                               /* version 3, as Phase 3 wrote it (380 bytes); version 2 the same */
    uint32_t magic;                            /* without song (364 bytes) */
    uint16_t ver, ntrk;
    uint32_t proj_crc;
    uch_trk8_t c[NTRK];
    int16_t song[8];
} uch_v3_t;
typedef struct {                               /* version 1, as Phase 1 wrote it (36 bytes) */
    uint32_t magic;
    uint16_t ver, ntrk;
    uint32_t proj_crc;
    uint8_t c[NTRK][6];                        /* type[5], bypass */
} uch_v1_t;
_Static_assert(sizeof(uch_trk_t) == 128u && sizeof(uch_store_t) == UCH_V4_BYTES + NTRK * 8u, "UMB1 v5 layout");
_Static_assert(UMOD_N == 2 && UMOD_NP == 4 && UMOD_DEST_N <= 127, "UMB1 v5 stores 2 MODS x 4 values (int8)");
_Static_assert(sizeof(uch_v3_t) == 12u + NTRK * 88u + 16u, "UMB1 v3 layout");
_Static_assert(US_N == 8, "UMB1 stores 8 song values");
_Static_assert(sizeof(uch_v1_t) == 36u, "UMB1 v1 layout");
_Static_assert(DEV_COUNT == 5 && DEV_NP == UCH_NP, "UMB1 v4 stores 5 devices x 12 values");
#ifdef ST_PAYLOAD_MAX
_Static_assert(sizeof(uch_store_t) <= ST_PAYLOAD_MAX, "UMB1: one storage object");
#endif
static uch_store_t uch_slot[4] __attribute__((section(".pool")));   /* the slots' chain records (v3) */
static uch_store_t uch_tmp __attribute__((section(".pool")));       /* the autosave's (main loop only) */

static uint32_t uch_crc(const void *p, uint32_t n)   /* FNV-1a; never 0 (0: no project) */
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

/* a record read from flash (n bytes in s) as v4: v1, v2 and v3 converted in place (what they lack: 0x7FFF, "the
 * default"), anything else cleared. 1 = a record */
#define UCH_DEF 0x7FFF
static int uch_take(uch_store_t *s, int n)
{
    uint32_t i, d, k;
    int v1 = n == (int)sizeof(uch_v1_t) && s->ver == 1u;
    int v2 = n == (int)(sizeof(uch_v3_t) - 16u) && s->ver == 2u, v3 = n == (int)sizeof(uch_v3_t) && s->ver == 3u;
    if ((v1 || v2 || v3) && s->magic == UCH_MAGIC && s->ntrk == NTRK) {
        static uch_v3_t o;                     /* (main loop only; v1 read into it as its first bytes) */
        memcpy(&o, s, v1 ? sizeof(uch_v1_t) : v2 ? sizeof(uch_v3_t) - 16u : sizeof(uch_v3_t));
        memset(s->c, 0, sizeof s->c);
        s->ver = UCH_VER;
        for (i = 0; i < NTRK; i++) {
            for (d = 0; d < 5u; d++) {
                s->c[i].type[d] = v1 ? ((const uch_v1_t *)(const void *)&o)->c[i][d] : o.c[i].type[d];
                for (k = 0; k < UCH_NP; k++)
                    s->c[i].p[d][k] = v1 || k >= 8u ? UCH_DEF : o.c[i].p[d][k];
            }
            s->c[i].bypass = v1 ? ((const uch_v1_t *)(const void *)&o)->c[i][5] : o.c[i].bypass;
        }
        for (k = 0; k < 8u; k++)
            s->song[k] = v3 ? o.song[k] : UCH_DEF;
        memset(s->mod, UCH_MOD_DEF, sizeof s->mod);   /* (no MODS: the defaults) */
        return 1;
    }
    if (n == (int)UCH_V4_BYTES && s->magic == UCH_MAGIC && s->ver == 4u && s->ntrk == NTRK) {   /* (v4: no MODS) */
        memset(s->mod, UCH_MOD_DEF, sizeof s->mod);   /* (the defaults, DEST OFF) */
        s->ver = UCH_VER;
        return 1;
    }
    if (n == (int)sizeof *s && s->magic == UCH_MAGIC && s->ver == UCH_VER && s->ntrk == NTRK)
        return 1;
    memset(s, 0, sizeof *s);
    return 0;
}

/* the chain playing now -> s, for the project record of CRC crc */
static void uch_capture(uch_store_t *s, uint32_t crc)
{
    uint32_t i, d, k;
    memset(s, 0, sizeof *s);
    s->magic = UCH_MAGIC;
    s->ver = UCH_VER;
    s->ntrk = NTRK;
    s->proj_crc = crc;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++) {
            s->c[i].type[d] = uchain[i].type[d];
            for (k = 0; k < DEV_NP; k++)
                s->c[i].p[d][k] = uchain[i].p[d][k];
        }
        s->c[i].bypass = uchain[i].bypass;
    }
    for (k = 0; k < US_N; k++)
        s->song[k] = k == US_FRZ ? 0 : usong[k];   /* (FREEZE: a performance control, never saved) */
    for (i = 0; i < NTRK; i++)
        for (d = 0; d < UMOD_N; d++)
            for (k = 0; k < UMOD_NP; k++)
                s->mod[i][d][k] = (int8_t)umod[i][d][k];
}

/* s -> the chain playing, when it is the chain of the project record of CRC crc; else the default chain. 1 = taken.
 * A type this build lacks: the first; a value out of its range: clamped; UCH_DEF: the type's default */
static int uch_apply(const uch_store_t *s, uint32_t crc)
{
    uint32_t i, d, k;
    chain_reset();
    if (s->magic != UCH_MAGIC || s->ver != UCH_VER || s->ntrk != NTRK || !crc || s->proj_crc != crc)
        return 0;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++) {
            const param_desc_t *pd;
            chain_set_type(i, d, s->c[i].type[d]);      /* (its defaults) */
            pd = dev_type(i, d)->pd;
            for (k = 0; pd && k < DEV_NP; k++)
                if (s->c[i].p[d][k] != UCH_DEF)
                    uchain[i].p[d][k] = (int16_t)clamp(s->c[i].p[d][k], pd[k].min, pd[k].max);
        }
        uchain[i].bypass = (uint8_t)(s->c[i].bypass & ((1u << DEV_COUNT) - 1u));
    }
    for (k = 0; k < US_N; k++)                 /* the song's values (chain_reset: their defaults, FREEZE off) */
        if (k != US_FRZ && s->song[k] != UCH_DEF)
            usong[k] = (int16_t)clamp(s->song[k], US_PD[k].min, US_PD[k].max);
    for (i = 0; i < NTRK; i++)                 /* the MODS (chain_reset: their defaults); UCH_MOD_DEF: the default */
        for (d = 0; d < UMOD_N; d++)
            for (k = 0; k < UMOD_NP; k++)
                if (s->mod[i][d][k] != (int8_t)UCH_MOD_DEF)
                    umod[i][d][k] = (int16_t)clamp(s->mod[i][d][k], UMOD_PD[k].min, UMOD_PD[k].max);
    return 1;
}

/* project.c's hooks */
static void uch_saved(uint32_t slot)          /* the project went into slot (proj_slot[slot] holds its record) */
{
    slot &= 3u;
    uch_capture(&uch_slot[slot], uch_crc(&proj_slot[slot], sizeof(project_store_t)));
#if UMBRA_FLASH
    if (flash_ok)
        st_save(OBJ_UCHAIN0 + slot, &uch_slot[slot], sizeof uch_slot[slot]);   /* (a failure: the RAM copy serves
                                                                                 * until power-off) */
#endif
}
static void uch_loaded(uint32_t slot)         /* slot's project is playing now */
{
    slot &= 3u;
    uch_apply(&uch_slot[slot], uch_crc(&proj_slot[slot], sizeof(project_store_t)));
}
static void uch_erased(uint32_t slot)          /* (the flash record stays: it names the erased project's record, so no
                                               * later project takes it; no erase, no wear) */
{
    memset(&uch_slot[slot & 3u], 0, sizeof uch_slot[slot & 3u]);
}
#if UMBRA_FLASH
static void uch_boot(void)                     /* power-on: the slots' chain records from flash */
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        uch_take(&uch_slot[i], flash_ok ? st_load(OBJ_UCHAIN0 + i, &uch_slot[i], sizeof uch_slot[i]) : -1);
}
static void uch_autosaved(const void *rec, uint32_t n)   /* the autosave wrote the project record rec (n bytes) */
{
    uch_capture(&uch_tmp, uch_crc(rec, n));
    st_save(OBJ_UCHAIN_AUTO, &uch_tmp, sizeof uch_tmp);
}
static void uch_autorestored(const void *rec, uint32_t n)   /* the autosave's record rec was restored */
{
    uch_take(&uch_tmp, st_load(OBJ_UCHAIN_AUTO, &uch_tmp, sizeof uch_tmp));
    uch_apply(&uch_tmp, uch_crc(rec, n));
}
#endif
