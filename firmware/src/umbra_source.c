/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA SOURCE (docs/DESIGN.md §3.1): where a track's sound comes from. Types (stored, in this order):
 *   SYNTH   the track's engine (voice.c track_render), as ever
 *   TAPE    a loop recorder (umbra_tape.c)
 *   SAMPLE  the SAMPLE engine (its flash sets and user slots): picking it loads that engine; then as SYNTH
 *   RETURN  no sound of its own: another track's output after its chain (FROM), at LEVEL
 *   OFF     silence (the engine still takes its notes: switching back is instant)
 * RETURN reads the block before (a double buffer, chain_post writes, mix_block swaps): one block late (0.7 ms),
 * the same on one core or two, whatever order the tracks render in. */
enum { SRC_SYNTH, SRC_TAPE, SRC_SAMPLE, SRC_RETURN, SRC_OFF };
enum { RET_FROM, RET_LEVEL };
static const param_desc_t SRC_PD_RETURN[DEV_NP] = {
    {"FROM", F_INT, 1, 4, 1, 0, 0},
    {"LVL", F_DB, 0, 127, 112, 0, 0},                   /* 0 dB (LEVEL_Q12 112 = 4096) */
};
static int32_t src_ret[2][NTRK][CTL];                   /* [0] this block's (written), [1] the one before (read) */

/* after a track's chain, before its level, pan and sends (fx.c mix_part): its block for the RETURNs */
static __attribute__((noinline)) void src_post(uint32_t tr, const int32_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < CTL; i++)
        src_ret[0][tr][i] = b[i];
}
/* the start of a block (fx.c mix_block, before the tracks): last block's outputs to be read */
static __attribute__((noinline)) void src_swap(void)
{
    memcpy(src_ret[1], src_ret[0], sizeof src_ret[1]);
    memset(src_ret[0], 0, sizeof src_ret[0]);
}
/* RETURN: track tr's block from track FROM's last one, at LEVEL (itself: silence) */
static __attribute__((noinline)) void src_return(uint32_t tr, const int16_t *p, int32_t *b, uint32_t n)
{
    uint32_t i, from = (uint32_t)(p[RET_FROM] - 1) % NTRK;
    int32_t g = LEVEL_Q12[p[RET_LEVEL] & 127];
    for (i = 0; i < n; i++)
        b[i] = from == tr ? 0 : (int32_t)(((int64_t)src_ret[1][from][i % CTL] * g) >> 12);
}
