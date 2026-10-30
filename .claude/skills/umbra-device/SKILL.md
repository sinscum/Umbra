---
name: umbra-device
description: Use when adding or changing a UMBRA chain device (SOURCE, GRANULAR, FILTER, DRIVE, SPACE), a device type inside one, or a MOD modulator — spec, interface, fixed-point DSP, budget, tests, UI page.
---

# Adding a chain device or device type

Read `docs/DESIGN.md` §3 for the device and `docs/HARDWARE.md` for real numbers first. Use with `fm1-dev-loop`.

## 1. Spec (write it in the PR/commit body before code)
One paragraph: what it sounds like (in dark-music words), its params as pages of exactly 4 (knob 1–4), ranges and units shown on screen, default values, and estimated CPU per track as a multiple of an ANALOG voice. Name the reference sound or recipe (DESIGN §9) it should make.

## 2. Interface — every device slot implements the same shape
Mirror the chain skeleton from Phase 1 (find it with `grep -rn "umbra_dev" firmware/src`). Each device type provides:
- `init(state*)` — static state only, no allocation.
- `set_param(state*, idx, value)` — called at control rate; convert UI values to fixed-point coefficients **here**, not per sample.
- `render(state*, in*, out*, n)` — block render; bypass is handled by the chain, not the device.
- `reset(state*)` — clear tails/buffers without clicks (short fade).
- a const descriptor: name (≤ 6 chars for the chain strip), param names (≤ 4 chars for knob cards), ranges, CPU cost constant, flag.

## 3. DSP rules
- Fixed-point (Q15/Q31 as the surrounding code does). Tables for tanh, sin, windows, pitch ratios — rasterise at build time like the base tree's assets.
- Smooth every knob-driven gain/cutoff (one-pole on the coefficient) to avoid zipper noise.
- Feedback paths (comb, resonator, delay, reverb, FREEZE): saturate, clamp coefficients < 1.0 except FREEZE's explicit unity hold, and DC-block.
- Cheapest first: control-rate modulation, fewer bands/grains, linear interpolation. Port float references (DaisySP etc.) PHYS-style and compare against the float original in a host test when one exists.

## 4. Device-specific notes
- **SOURCE SYNTH** wraps existing engines; don't fork engine code.
- **GRANULAR** owns no memory — it reads a buffer from `umbra-buffers`. Grain scheduling at control rate; per-sample work bounded.
- **FILTER RESONATOR** starts at 6 bands; the descriptor must allow dropping to 4 under load.
- **DRIVE** order is fixed: drive → fold → crush → decimate → noise → tilt → comp.
- **SPACE** reverb/delay are shared buses; per-track device only owns sends/pan/width.
- **MOD** modulators run once per block and write into a per-track mod array the devices read in `set_param`.

## 5. Ship checklist
- `UMBRA_<DEVICE>[_<TYPE>]` flag, builds and tests at 0 and 1.
- Presets/recipes using it; golden renders added only with Shane's OK.
- CPU line in the budget files, measured; RAM line if it has state > 1 KB.
- UI page passes the UI test in every palette; device visual (DESIGN §8) drawn.
- Saved-data: params added to the project format with a version bump + migration test.
- Wrap-up with what to listen for: name the patch, the knob, the expected sound.
