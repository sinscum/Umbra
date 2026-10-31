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

#define UMBRA_MEM_PRE (2208u * 2u)              /* SPACE PRE-DLY: 100 ms at 22.05 kHz (2205 pairs), int16 (umbra_space.c) */
#ifndef UMBRA_SHIMMER
#define UMBRA_SHIMMER 0
#endif
#define UMBRA_MEM_SHIM (UMBRA_SHIMMER ? 1024u * 2u : 0u)   /* SPACE SHIMMER's line (UMBRA_SHIMMER only) */

#define UMBRA_GRN_RING 11264u                   /* GRANULAR's ring a track: 0.51 s at 22.05 kHz, mu-law (umbra_granular.c) */
#define UMBRA_MEM_GRN (4u * UMBRA_GRN_RING)     /* 44 KiB (Phase 4: the delay's freed half, fx.c UMBRA_DLY_HALF) */

#define UMBRA_TAPE_BYTES 40960u                 /* SOURCE TAPE, shared by the tracks set to it: 1.86 s LO / 0.93 s HI at
                                                 * 22.05 kHz for one (umbra_tape.c) */
#define UMBRA_MEM_TAPE UMBRA_TAPE_BYTES

#define UMBRA_MEM_TOTAL (UMBRA_MEM_COMB + UMBRA_MEM_PRE + UMBRA_MEM_SHIM + UMBRA_MEM_GRN + UMBRA_MEM_TAPE)   /* the
                                                                                      * sum of every UMBRA_MEM_<NAME> */

#endif
