/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's saved chain ("UMB1", version 1): each project slot's tracks' chains (umbra_chain.c: types and bypasses),
 * beside the project in a storage object of its own (storage.c OBJ_UCHAIN0.., A/B at 0xE8000..: the BSP's FL_XDATA1,
 * a firmware's own data), and the autosave's (OBJ_UCHAIN_AUTO). The project record stays the base tree's (a full
 * flash sector: no room), so a project saved by the base tree, or by UMBRA before the chain, still loads.
 *
 * A chain record names the project record it was saved with (proj_crc: the record's CRC, as packed): a project
 * loaded whose record is not that one (none saved with it, the slot re-saved by the base tree since, erased, a torn
 * write) gets the default chain (every slot its first type, nothing bypassed). Included by project.c, after its
 * staging (proj_wire, proj_slot). Main loop only. */
#define UCH_MAGIC 0x31424D55u                  /* "UMB1" */
#define UCH_VER 1u
typedef struct {
    uint32_t magic;
    uint16_t ver, ntrk;
    uint32_t proj_crc;                         /* the project record's (uch_crc), 0 = none */
    umbra_chain_t c[NTRK];
    uint8_t pad[(4u - (NTRK * sizeof(umbra_chain_t)) % 4u) % 4u];
} uch_store_t;
static uch_store_t uch_slot[4] __attribute__((section(".pool")));   /* the slots' chain records, as on flash */

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

/* the chain playing now -> s, for the project record of CRC crc */
static void uch_capture(uch_store_t *s, uint32_t crc)
{
    uint32_t i, d;
    memset(s, 0, sizeof *s);
    s->magic = UCH_MAGIC;
    s->ver = UCH_VER;
    s->ntrk = NTRK;
    s->proj_crc = crc;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++)
            s->c[i].type[d] = uchain[i].type[d];
        s->c[i].bypass = uchain[i].bypass;
    }
}

/* s -> the chain playing, when it is the chain of the project record of CRC crc; else the default chain. 1 = taken */
static int uch_apply(const uch_store_t *s, uint32_t crc)
{
    uint32_t i, d;
    chain_reset();
    if (s->magic != UCH_MAGIC || s->ver != UCH_VER || s->ntrk != NTRK || !crc || s->proj_crc != crc)
        return 0;
    for (i = 0; i < NTRK; i++) {
        for (d = 0; d < DEV_COUNT; d++)
            chain_set_type(i, d, s->c[i].type[d]);      /* (a type this build lacks: refused, the first) */
        uchain[i].bypass = (uint8_t)(s->c[i].bypass & ((1u << DEV_COUNT) - 1u));
    }
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
        if (!flash_ok || st_load(OBJ_UCHAIN0 + i, &uch_slot[i], sizeof uch_slot[i]) != (int)sizeof uch_slot[i])
            memset(&uch_slot[i], 0, sizeof uch_slot[i]);
}
static void uch_autosaved(const void *rec, uint32_t n)   /* the autosave wrote the project record rec (n bytes) */
{
    uch_store_t s;
    uch_capture(&s, uch_crc(rec, n));
    st_save(OBJ_UCHAIN_AUTO, &s, sizeof s);
}
static void uch_autorestored(const void *rec, uint32_t n)   /* the autosave's record rec was restored */
{
    uch_store_t s;
    if (st_load(OBJ_UCHAIN_AUTO, &s, sizeof s) != (int)sizeof s)
        memset(&s, 0, sizeof s);
    uch_apply(&s, uch_crc(rec, n));
}
#endif
