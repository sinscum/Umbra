/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's audio memory: every audio buffer UMBRA adds is sized here, and only here, so the total is known at build
 * time (tests/ram_test.c checks it against tests/ram_budget.txt). No malloc anywhere in audio code.
 *
 * Where the room comes from (docs/HARDWARE.md): the pool has 41.4 KiB free and the build keeps 8 KiB of it spare,
 * so new buffers get ~33 KiB. More needs the base tree's own buffers (the delay's 128 KiB, the SLICER's 32 KiB),
 * replaced or shared, never added to (umbra-buffers skill).
 *
 * Sizes are in bytes, derived from a rate and a length so the arithmetic is visible. Phase 1 declares the scheme
 * with no buffers yet: each later device adds its line to UMBRA_MEM_TOTAL. */
#ifndef UMBRA_MEM_H
#define UMBRA_MEM_H

#define UMBRA_MEM_FS 44100u                     /* the engine rate (FS, build/gen/umbra_tables.h) */
#define UMBRA_MEM_FS_LO 22050u                  /* half rate: tape / grain buffers that can afford the dulling */

/* bytes for MS milliseconds at RATE: 16-bit (HI) or 8-bit mu-law (LO) */
#define UMBRA_MEM_HI(rate, ms) ((rate) * (ms) / 1000u * 2u)
#define UMBRA_MEM_LO(rate, ms) ((rate) * (ms) / 1000u)

/* ---- the buffers ----------------------------------------------------------------------------------------------
 * Phase 4 adds: per-track GRANULAR rings, TAPE; Phase 3: the SPACE delay (sharing the base tree's). Each as
 *   #define UMBRA_MEM_<NAME> <bytes>     and its term in the sum below. */
#define UMBRA_COMB_LEN 1024u                    /* FILTER COMB's delay line a track: 1023 samples, its lowest note
                                                 * ~43 Hz (F1), int16 (umbra_filter.c) */
#define UMBRA_MEM_COMB (4u * UMBRA_COMB_LEN * 2u)   /* 8 KiB */

#define UMBRA_MEM_TOTAL (UMBRA_MEM_COMB)        /* the sum of every UMBRA_MEM_<NAME> above */

#endif
