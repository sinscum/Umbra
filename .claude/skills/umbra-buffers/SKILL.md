---
name: umbra-buffers
description: Use for any UMBRA work that allocates or reads audio memory — TAPE recording, per-track ring buffers, the grain pool, delay/reverb lines, LO encoding, CLEAR — and for RAM budget failures.
---

# Audio memory on the FM-1

RAM, not just CPU, decides what UMBRA can be. Get numbers from `docs/HARDWARE.md`; if they're missing, measure first (Phase 0).

## Rules
- Every audio buffer is a static array declared in **one** place: `firmware/src/umbra_mem.h` / `.c`. Sizes are `#define`s derived from sample rate and seconds, so the total is computable at build time.
- `tests/ram_budget.txt` holds the total; a host test sums the buffers and fails if over. Never raise the budget to pass — shrink, share, or lower quality first, and ask Shane.
- No malloc, no buffer growth at runtime. Lengths change by changing how much of a fixed buffer is used.
- Buffers that are never used at the same time share memory (e.g. a track's ring buffer and its TAPE buffer can be a union if GRANULAR reads TAPE when it exists).

## Layout (starting point — replace sizes with Phase 0 figures)
- 4 × TAPE buffers (largest; LO mode doubles usable length).
- 4 × GRANULAR ring buffers (≥ 0.5 s target), or aliased onto TAPE.
- 1 × delay line (stereo, max time at lowest BPM sync value — clamp, don't extend).
- 1 × reverb (the base tree's existing reverb memory; reuse, don't duplicate).
- Grain pool: fixed array of grain structs, global cap + per-track cap, oldest-quietest stolen first with a short fade.

## TAPE details
- Write pointer advanced in the render callback; record arm/disarm and source switching take effect on block boundaries with a few-ms fade.
- LO = 8-bit µ-law (table encode/decode). Its grit is a feature; HI = 16-bit.
- Sources: MIXER (post-mix, pre-limiter), TRACK n (post-chain), SELF (overdub with feedback amount, saturating).
- CLEAR: zero by chunks across several blocks so it never spikes CPU; confirm dialog first.
- TAPE audio is not saved to flash in v1. Never write flash from the audio path.

## Reading
- Fractional read with linear interpolation; wrap with masks when lengths are powers of two, else compare-and-subtract.
- Reverse and varispeed read the same buffer; never copy audio to make a reversed version.

## Tests
- RAM total vs budget. Ring/TAPE wrap correctness at boundaries. Grain cap and stealing under a stress pattern (no clicks: check max sample-to-sample delta in the render). LO encode/decode round-trip error bounds.
