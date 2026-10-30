# UMBRA roadmap

Each phase ends with a demo Shane can hear in the browser emulator. Don't start a phase until the
previous one's acceptance list is ticked. Hardware tests only when Shane asks.

## Phase 0 — Fork, measure, decide (no new DSP)
- Fork the base tree and **rebrand it fully** per CLAUDE.md → Lineage: output names, package identity and version strings, `UMBRA_*` flags, boot screen, every user-facing string. Check what `fm1_install.py` and the web installer expect of the identity before changing it.
- `grep -ri` the tree for the old name: the only hits left should be LICENSE, LICENSING.md, CREDITS.md, copyright headers.
- Build + full test suite green on the unchanged fork.
- Write `docs/HARDWARE.md` from the code, not guesses: sample rate, block size, free RAM after boot (internal / PSRAM), flash map and space left, whether any audio input exists in the BSP, how the base tree's SAMPLE/SLICE and user sample slots load audio, CPU % of each existing engine at 4 voices from `tests/cpu_baseline.txt`.
- Skim FM1 Open's repo and licence; note anything useful (sample slots, audio paths) in HARDWARE.md.
- Propose the final control map (DESIGN §7) against the base tree's `docs/controls.jpg`.
- **Accept:** green build/tests, HARDWARE.md, control map approved, buffer sizes in DESIGN §2 replaced with real numbers.

## Phase 1 — Chain skeleton
- Track model: 4 tracks × 5 device slots, each slot bypassable, typed, with params and a declared CPU cost. SOURCE = SYNTH (existing engines) only; other devices are pass-through stubs.
- Device page layout (DESIGN §8.1: chain strip, value strip, placeholder visual area), track select, bypass, type select.
- Project format `UMBRA_PROJ v1` + migration test from a base-tree project.
- `firmware/src/umbra_mem.h` + `tests/ram_budget.txt` + host test.
- **Accept:** 4 tracks playing different engines through stub chains; UI test PNGs reviewed; CPU Δ ≈ 0.

## Phase 2 — DRIVE and FILTER (cheap, high payoff)
- DRIVE: drive (tanh table), fold, crush, decimate, noise, tilt, simple comp.
- FILTER: SVF morph first, then COMB, then RESONATOR (6 bands, scale/chord tuning), then FORMANT.
- **Accept:** golden renders added (with Shane's OK), per-device CPU lines, recipes "Sunken Pulse" and "Distant Choir" playable.

## Phase 3 — SPACE + mixer
- Shared reverb + delay buses, per-track sends, pan/width, FREEZE. Mixer page, master DJ filter, glue, limiter.
- SHIMMER behind `UMBRA_SHIMMER`, measured, off by default if expensive.
- **Accept:** 4 tracks + buses within budget; FREEZE clean (no clicks on engage/release).

## Phase 4 — Buffers: GRANULAR and TAPE
- Per-track ring buffer; grain pool (global/per-track caps); contour tables; scale lock.
- SOURCE TAPE: record MIXER / TRACK / SELF, HI and LO quality, loop + POLY play, CLEAR.
- SOURCE SAMPLE from flash slots; RETURN.
- **Accept:** "Dead Bells", "Worn Cathedral", "Shadow" recipes; RAM test green; grain stealing audible-but-graceful under overload.

## Phase 5 — MOD, macros, SCENES
- 2 control-rate mods per track (LFO shapes, ENV, FOLLOW, RANDOM), assign via hold LFO + knob. TEMP.
- 4 macros (MACROS page), 8 SCENES with morph.
- **Accept:** a SCENE morph across all 4 tracks without zipper noise; saved and reloaded.

## Phase 6 — The eclipse & the look
- The five device visuals (DESIGN §8.1) and the eclipse on home/mixer/boot (§8.2), 4 palettes, silent boot eclipse, removal of any base-tree UI sounds, a demo PROJECT.
- **Accept:** UI test green in all palettes, Shane signs off the look.

## Phase 7 — Web & release
- Emulator page (the base tree's `web/emu`), web editor for device params/scenes, cheat sheet page, README, CREDITS, release build `./build.sh --release 0.1`.
- **Accept:** release folder complete (package, app bin, SHA256SUMS, licences, attribution); install command printed for Shane.

## Later / maybe
- Saving TAPE audio to flash (v2 format). Dual-band DRIVE. 2nd-core option for SHIMMER/extra grains. MIDI-learn for macros.
