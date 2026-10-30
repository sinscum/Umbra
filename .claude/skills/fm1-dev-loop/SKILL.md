---
name: fm1-dev-loop
description: Use when building, testing, listening to, packaging or preparing a hardware test of UMBRA (base-tree FM-1 firmware) — the build loop, golden/CPU/RAM tests, emulator, and the never-flash rule.
---

# FM-1 dev loop (UMBRA)

CLAUDE.md wins over this skill if they disagree.

## 0. Preflight (once per session)
- `git submodule status` → empty engines/bsp → `git submodule update --init`.
- Toolchain `~/.jieli/toolchain`, SDK `~/fw-AC79_AIoT_SDK` (tag `AC79NN_SDK_V1.2.1_2023-12-13`); macOS: Docker running, tree under `/Users`.
- `pip3 install Pillow fonttools` (+ `mido python-rtmidi` for `--info`).

## 1. Plan
Files to touch · `UMBRA_*` flag · the test that proves it · expected CPU and RAM impact. Wait for a go on anything multi-file.

## 2. Build — browser first
`./build.sh --gen && web/emu/build.sh` (no toolchain/SDK). CI (`.github/workflows/emu.yml`) does the same on every push and publishes `main` to GitHub Pages. The device build (`./build.sh`) only after Shane has tested in the browser and asks.
On failure paste the first real error only. Usual causes: empty submodule, missing Pillow/fontTools, no emcc, Docker down, SDK path, a new file not added to the build list.

## 3. Test
Browser: `node web/emu/emu_test.mjs build/emu/umbra.wasm` (CI also compares it with the native build). Full `tests/run_tests.sh` once device builds start (needs `./build.sh` and `AC79_SDK`). Read failures by kind:
- **golden mismatch** → the sound changed. Intended? If not, it's a bug. Never `GOLDEN_UPDATE=1` without Shane's OK.
- **cpu_baseline / target_budget** → report % Δ per device/engine; never `BUDGET_UPDATE=1` to pass. Offer a cheaper design.
- **ram_budget** → report bytes Δ against `tests/ram_budget.txt`; shrink buffers or share them before raising the budget.
- **UI test** → open the touched screens' PNGs in `build/ui_new/`, all palettes.
- New flag: build + test with it at 0 and 1.

## 4. Listen
`web/emu/build.sh && python3 -m http.server -d build/emu 8790` → http://localhost:8790/. Tell Shane exactly what to load, what to play, which knob to turn, and what he should hear. The emulator doesn't cover USB audio/MIDI out, TRS MIDI or user sample slots.
Optional second opinion: load the `.fwsc` in fm1-emu (joakimeriksson).

## 5. Hardware — Shane runs it, never Claude
Print this checklist and stop:
1. M-VAVE stock V15 `FM-1.fwsc` downloaded.
2. `python3 tools/fm1_install.py --info`
3. `python3 tools/fm1_install.py build/<pkg>.fwsc`
4. Black screen → USB device WL80UBOOT (4C4A:8057) → FM-1-transporter recovery, back up flash first.

## 6. Release
`./build.sh --release X.Y` → `build/release-X.Y/`: package, app bin, SHA256SUMS, LICENSE, LICENSING.md, LICENSES/, sample attribution. Release notes: what's new, what to listen for, saved-data upgrade notes, SHA-256s.

## Wrap-up
**Changed · Tests · CPU Δ · RAM Δ · Listen for · Next.**
