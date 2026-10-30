# Handing UMBRA to Claude Code

## 1. Create the repo (on your Mac)

```
gh repo fork hugelton/Felucca --fork-name umbra --clone=false
git clone --recurse-submodules https://github.com/<you>/umbra.git ~/umbra
cd ~/umbra
```
(Keep it under `/Users` so Docker can share it. If you'd rather start a fresh repo than a GitHub fork, clone Felucca with `--recurse-submodules`, re-point `origin` at a new empty `umbra` repo, and push — the GPL only needs the licence files and credits kept.)

## 2. Drop in the bundle

Unzip `umbra-handoff.zip` and copy its contents into the repo root:

```
CLAUDE.md                          replaces the existing CLAUDE.md (if any)
HANDOFF.md                         this file
docs/DESIGN.md                     the product: chain, controls, screens, naming rule
docs/ROADMAP.md                    Phases 0–7 with acceptance checks
docs/mockups/umbra-screen.html     interactive screen reference
.claude/skills/fm1-dev-loop/       build · test · listen · never flash
.claude/skills/umbra-device/       adding a chain device or mod
.claude/skills/umbra-buffers/      TAPE, grains, delay/reverb memory
.claude/skills/umbra-ui/           screens, visuals, eclipse, palettes
```

```
git add CLAUDE.md HANDOFF.md docs .claude
git commit -m "docs: UMBRA design, roadmap and Claude Code skills"
git push
```

## 3. Check prerequisites (once)

- Docker Desktop running
- `pip3 install Pillow fonttools mido python-rtmidi`
- `tools/get_toolchain.sh` → `~/.jieli/toolchain`
- `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK`
- Node.js and Emscripten (the browser emulator — UMBRA's test target). Or skip local builds: CI builds the emulator on every push and publishes it to GitHub Pages (repo Settings → Pages → Source: **GitHub Actions**).
- The toolchain and SDK above are only needed later, for device firmware.
- Keep M-VAVE's stock V15 `FM-1.fwsc` downloaded before any hardware test

## 4. Start Claude Code in the repo and paste this

> Read CLAUDE.md, docs/DESIGN.md and docs/ROADMAP.md, and open docs/mockups/umbra-screen.html for the screen layout. We're doing **Phase 0 only** — no new DSP and no device changes.
> 1. Get the browser emulator green on the unchanged fork (`./build.sh --gen`, `web/emu/build.sh`, the emu CI workflow, live on GitHub Pages). No device firmware yet.
> 2. Write `docs/HARDWARE.md` from the code: sample rate, block size, free RAM after boot (and where it lives), flash map and free space, any audio input path in the BSP, how SAMPLE/SLICE and user slots load audio, and each engine's CPU % from the baseline files. Mark anything you couldn't confirm.
> 3. Skim m-vave/fm1_open: its licence, and any hardware facts that fill gaps in HARDWARE.md.
> 4. Propose the final button map for DESIGN §7 against the base tree's controls.
> 5. Propose the full rebrand to UMBRA (CLAUDE.md → Lineage): output names, package identity, flags, strings — and what the installer checks.
> Give me a plan before changing any files. End with Changed · Tests · CPU Δ · RAM Δ · Listen for · Next.

## 5. After Phase 0

Review HARDWARE.md and the proposals, approve the button map and the rebrand, then tell Claude Code
"start Phase 1". One phase at a time; each ends with something to hear in the browser emulator.

## Decisions still open (DESIGN §12)
- Keep the base tree's sequencer, or go tape-first?
- Stay on the current base tree, or switch to M-VAVE's FM1 Open after Phase 0 reports on its licence?
