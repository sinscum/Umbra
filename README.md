# UMBRA

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

> The umbra is the darkest part of a shadow — the place no light reaches during an eclipse.

UMBRA is a dark ambient instrument firmware for the M-VAVE FM-1: four tracks, each run through a chain
(SOURCE → GRANULAR → FILTER → DRIVE → SPACE) that turns a source into slow, decaying texture.
See [docs/DESIGN.md](docs/DESIGN.md) for the instrument and [docs/ROADMAP.md](docs/ROADMAP.md) for where it is.

**Status: early development (Phase 3).** The chain is in place; FILTER (SVF, COMB, RESONATOR, FORMANT) and DRIVE
(DAMAGE) have their own sound; SPACE has WOW, PRE-DLY and FREEZE; MASTER a DJ filter and GLUE; MIXER mute / solo keys.
GRANULAR and TAPE come next.

- **Play it in your browser:** <https://sinscum.github.io/Umbra/> — no FM-1 needed. Every change is tested here first.
  **Load demo** (after Switch on) picks a recipe: Four engines, Sunken Pulse, Distant Choir, Frozen Hall; then press PLAY.
- **On the FM-1:** no device builds are published yet.
- Hardware facts: [docs/HARDWARE.md](docs/HARDWARE.md) · Build: [BUILDING.md](BUILDING.md)

## Credits and licence

UMBRA is built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton
Instruments (GPL-3.0-only), with its engines, sequencer, storage, USB, tests and browser emulator.

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the hardware layer in `firmware/bsp` is MPL-2.0
([felucca-bsp](https://github.com/hugelton/felucca-bsp)); the bundled fonts and ported DSP keep their own licences
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work is in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. UMBRA is not affiliated with or endorsed by them.
