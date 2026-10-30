# CLAUDE.md — UMBRA (FM-1 custom firmware)

Instructions for Claude Code in this repo. Read this first, every session, then `docs/DESIGN.md`
and `docs/ROADMAP.md`. If they disagree, this file wins on process, DESIGN.md wins on product.

## What this is

**UMBRA** — a dark ambient instrument firmware for the **M-VAVE FM-1** (JieLi AC791N / WL82, pi32v2,
flash XIP at 0x02000000). Its own name, look, flags and identity.

**Naming rule** (`docs/DESIGN.md` → Naming rule): controls, devices, parameters and menus use plain
names; samples, presets, factory projects and palettes get the dark-ambient flavour.

Four tracks, each a fixed chain **SOURCE → GRANULAR → FILTER → DRIVE → SPACE**, plus MIXER, MODS
(2 per track), MACROS and SCENES. Workflow inspired by the Torso S-4; not a port — no S-4 code,
branding, artwork or manual wording anywhere.

Sound: dark ambient first — drones, slow-moving texture, decayed tape, distant tonal resonance —
with room for noise and darker rhythmic work. Look: a slow, sparse eclipse on the 240×240 TFT; no
mascot. **No UI sounds and no boot tune** — the instrument is silent except for the music.
Also ships: browser emulator page, web editor, cheat sheet — GitHub Pages.

## Lineage (internal — never user-facing)
The code tree is a fork of **Felucca** (hugelton/Felucca, GPL-3.0-only); its `BUILDING.md` is the
source of truth for toolchain, tests and emulator until UMBRA's own replaces it. Phase 0 rebrands it:
- outputs `felucca.bin` / `felucca.fwsc` → `umbra.bin` / `umbra.fwsc`; package identity and version strings;
- `FELUCCA_*` build flags → `UMBRA_*` (same defaults; build green with each at 0 and 1);
- every user-facing string, the boot screen, web pages, README.
Felucca's name stays **only** in `LICENSE`, `LICENSING.md`, `CREDITS.md`, copyright headers of files
derived from it, and git history — the GPL requires that attribution.
No other firmware names, earlier working names, themes or projects appear anywhere in this repo.

## Reference trees (read, don't blindly copy)
- **Skiff** (illuminus5/skiff) — distortion, slicer, LFO, arp, sequencer automation, mixer. Licence check first.
- **Grimm** (GRiMMBIENT/grimm) — UI/design ideas only.
- **FM1 Open** (m-vave/fm1_open) — M-VAVE's official open firmware + SDK (Oct 2026). Read its licence first; useful for hardware facts.
- **fm1-emu** (joakimeriksson) — second-opinion emulator for `.fwsc` packages.
- **Torso S-4** — workflow reference only (manuals, reviews). Behaviour, not code.

## Before you touch code
1. `git submodule status`; empty `firmware/engines/` or `firmware/bsp/` → `git submodule update --init`.
2. Toolchain `~/.jieli/toolchain` (clang 4.0.1, `tools/get_toolchain.sh`); SDK `~/fw-AC79_AIoT_SDK` tag `AC79NN_SDK_V1.2.1_2023-12-13`. Override with `JIELI_TOOLCHAIN` / `AC79_SDK`.
3. macOS: Docker Desktop running, tree under `/Users`.
4. Read the files you'll change and their tests. Don't guess BSP or engine APIs — mirror the closest existing engine/FX.
5. Check `docs/HARDWARE.md` (written in Phase 0). If a number isn't there, measure it.

## The loop (every change) — browser first
```
./build.sh --gen              # build/gen only (fonts, tables, samples); no JieLi toolchain or SDK
web/emu/build.sh && python3 -m http.server -d build/emu 8790    # play it at http://localhost:8790/
```
Every push also builds and tests the emulator in CI (`.github/workflows/emu.yml`) and publishes it from `main`
to GitHub Pages, so each change can be played in a browser without building anything.

Done = emulator builds, its test passes, heard in the browser. **No device firmware** (`./build.sh` without
`--gen`, `.fwsc` packages, `tests/run_tests.sh`'s target checks) until Shane has tested the change in the
browser and asks for it. Hardware last, only when Shane says so.

## Hard rules
- **Browser before device.** Every feature must be playable in the browser emulator (`web/emu`); if the
  emulator can't show it, extend the emulator in the same change. Don't build or prepare device packages until
  Shane has tested in the browser and asks.
- **Never flash.** Never run `fm1_install.py` without `--info`, or any write to the device. Prepare the package, print the exact command, stop.
- **Golden hashes and budgets are tripwires.** Never `GOLDEN_UPDATE=1` / `BUDGET_UPDATE=1` to make a test pass; only after Shane confirms the change was intended, listing which renders changed and why.
- **CPU budget is the real constraint.** Target: 4 tracks, full chain, defaults ≤ 70 % of one core. Report the delta after any DSP change. Over budget → cheaper version first (fewer grains/bands/voices, control-rate modulation, tables), 2nd core last.
- **RAM budget is the second constraint.** All audio buffers static, sized in one header (`firmware/src/umbra_mem.h`), checked against `tests/ram_budget.txt`. No malloc in audio code.
- **Audio path is fixed-point and real-time.** No malloc, blocking, printf or flash writes in render. Saturate, never wrap. Follow PHYS for float→fixed ports.
- **One feature per branch, small commits:** `area: what changed` (e.g. `granular: add grain pool cap`).
- **Every device/feature gets an `UMBRA_*` 0/1 flag**; build + tests pass with it on and off.
- **USB descriptors are fragile** (`firmware/src/usb.c`). Don't touch unless that's the task.
- **Don't break saved data.** Layout change → version bump, migration, test loading the old format. Projects saved by the base tree load or fail cleanly with a message.
- **Every new screen passes the UI test** in every palette; look at the PNGs in `build/ui_new/`.
- **No UI sounds.** Don't add beeps, clicks or jingles; remove any the base tree plays.

## Licensing
- GPL-3.0-only, source published. Engines submodule MIT; BSP MPL-2.0 (BSP changes stay MPL); AC79 SDK files Apache-2.0 (licence texts ship); CC0 samples keep `ATTRIBUTION.txt`.
- Skiff, Grimm, FM1 Open: read the LICENSE before porting. Unclear → reimplement from behaviour and say so in the commit.
- Credit every source in `CREDITS.md`.

## Recovery facts
- Black screen: look for USB device **WL80UBOOT** (4C4A:8057) — chip boot mode.
- Recovery: FM-1-transporter (kurogedelic) via Seeed XIAO RP2040; back up flash first, then M-VAVE's `FM-1.fwsc` (V15). Keep the stock package downloaded before any hardware test.

## Skills in this repo (`.claude/skills/`)
- `fm1-dev-loop` — build / test / listen / package, safe hardware handoff.
- `umbra-device` — adding or changing a chain device or a MOD.
- `umbra-buffers` — TAPE, ring buffers, grains, delay/reverb memory.
- `umbra-ui` — screens, the eclipse, palettes.

## Working with Shane
- Experienced IT/infrastructure person, new-ish to embedded DSP. Explain DSP/fixed-point choices in a sentence or two; skip git/Docker/shell basics.
- Before multi-file changes: short plan (files, risk, test) and wait for a go.
- Something sounds wrong → ask him to describe it (thin, clicky, aliased, quiet, zipper) and map to a cause before editing.
- End every task with: **Changed · Tests · CPU Δ · RAM Δ · Listen for · Next.**
