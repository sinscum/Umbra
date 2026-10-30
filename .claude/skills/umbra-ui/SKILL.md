---
name: umbra-ui
description: Use when drawing or changing any UMBRA screen — device pages and their full-screen visuals, the value strip, track tabs, the chain strip, the eclipse, palettes, the boot sequence — or when removing UI sounds.
---

# UMBRA screens

Spec: `docs/DESIGN.md` §8. Approved layout/motion reference: `docs/mockups/umbra-screen.html` — match its layout, spacing and feel; it is not final art. The base tree's UI code is the drawing layer — extend it, don't replace it.

## Two kinds of screen (240×240)
- **Device pages** (DESIGN §8.1): top bar (track tabs, scene, tempo) → chain strip (open device boxed, bypassed hollow) → **value strip** with the same 4 params as the knobs, same order → **one full-width visual** for the open device → a line with the type and one piece of context.
- **Home and mixer** (DESIGN §8.2): the eclipse, with mixer levels in the value cards.
- Values: name ≤ 4 chars, value with unit; read from a lock-free single-writer snapshot the audio side publishes once per block. Never read audio buffers mid-write — the waveform views draw from a decimated copy the audio side updates.
- No clipped or overlapping text in any palette — the UI test enforces it; look at the PNGs in `build/ui_new/`.

## Device visuals (one each, original drawings)
- **SOURCE**: dim waveform + slow playhead; loop brackets; reverse = playhead moving right-to-left; SYNTH shows an output scope.
- **GRANULAR**: faint waveform; grains as specks that appear where they read, drift up and fade; SPEED 0 leaves them hanging.
- **FILTER**: thin vertical band lines at their frequencies, glowing as they ring; SVF = one soft response curve.
- **DRIVE**: transfer curve — straight when clean, bending with DRIVE, folding with FOLD, stepped with CRUSH; NOISE as fine grain.
- **SPACE**: fading tail into darkness, delay taps as dim marks; FREEZE holds it still.
- Never trace or recreate another instrument's artwork; when unsure, simpler is better.

## The eclipse (home, mixer, boot)
- Disc coverage follows total output level (silence = thin crescent, loud = totality), smoothed ~1 s.
- The corona ring brightens and frays with the overall DRIVE amount.
- **FREEZE stops all motion** on every screen until released.
- Boot: black, a crescent closes slowly to totality, the name fades in. Silent.

## Motion
Slow and sparse. Nothing blinks or flashes. Cap draw time, skip frames under CPU load, never block input. Draw procedurally or from small palette-indexed assets rasterised at build time — no mascots, no characters.

## Palettes
TOTALITY (black, cold grey — default), ASH (charcoal and bone), BLOODMOON (black and deep rust), NOCTILUCENT (black and pale electric blue). Every palette defines the same named roles; screens use roles, never raw colours. Keep contrast readable on the small TFT.

## Sound
UMBRA has **no UI sounds**: no key clicks, save chimes, error beeps or boot tune. If the base tree plays any, remove them (behind no flag — they're simply gone). Confirmations are visual only ("SAVED" on save).

## Checklist
UI test green in all 4 palettes · PNGs reviewed and described to Shane · plain names on controls, flavoured names only on content (DESIGN → Naming rule) · strings fit their cards · draw time measured in the emulator.
