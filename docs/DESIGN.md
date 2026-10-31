# UMBRA — design document

> The umbra is the darkest part of a shadow — the place no light reaches during an eclipse.
> UMBRA is a dark ambient instrument for the M-VAVE FM-1: four tracks, each run through a chain that
> turns a source into slow, decaying texture.

### Naming rule
- **Anything you operate is named plainly**: devices, parameters, menus, pages, buttons, messages. A
  newcomer should know what a control does from its name.
- **Content gets the UMBRA flavour**: sample sets, sample slots, factory presets, factory projects,
  demo material, palettes. That's where the mood lives.

### Control vocabulary (plain)
| Thing | Name |
| --- | --- |
| Tracks | TRACK 1–4 |
| Chain devices | SOURCE → GRANULAR → FILTER → DRIVE → SPACE |
| Mixer | MIXER |
| Modulators | MODS (2 per track) |
| Macros | MACROS (4 per project) |
| Scenes | SCENES (8 per project) |
| Momentary tweak | TEMP |
| Save confirmation | "SAVED" |
| Clear recorded audio | CLEAR |
| A track fed by another track | RETURN (SOURCE type) |

### Content naming (flavoured) — examples, not a fixed list
- **Sample sets** (display names for the bundled CC0 instruments and drones): HOLLOW PIANO, CINDER STRINGS, DROWNED ORGAN, GRAVE CHOIR, IRON BELLS, ASH NOISE. Keep the original names and credit in `ATTRIBUTION.txt`; only the on-screen name changes.
- **User sample slots** default to SLOT 1–8 and take whatever name the user gives them.
- **Factory projects and presets**: see §9.
- **Palettes**: TOTALITY, ASH, BLOODMOON, NOCTILUCENT.

Workflow inspiration: the Torso S-4 "sculpting sampler" — parallel tracks, each a fixed chain of
devices that turn a source into texture. UMBRA is not a port: the S-4's firmware is closed and its
hardware is far bigger (quad-core, 4 GB, audio inputs, 9 encoders). We take the *idea* and size it to a
single-core pocket synth. Plain words like FILTER or SPACE are fine; no Torso/S-4 branding, artwork,
layout or manual wording anywhere user-facing.

Base tree: an existing GPL-3.0 FM-1 firmware (see CLAUDE.md → Lineage), kept for its build, tests,
emulator, USB, storage, sequencer and engines. UMBRA has its own name, identity, look and flags.

---

## 1. Workflow mapping (internal reference for Claude Code)

| Workflow idea | Reference instrument | UMBRA (FM-1) | Why it changes |
| --- | --- | --- | --- |
| Tracks | 4 parallel stereo tracks | **4 TRACKS**, mono through the chain, stereo at SPACE and MIXER | CPU; the FM-1 engine is mono |
| Device chain | source → granular → filter → colour → space | **SOURCE → GRANULAR → FILTER → DRIVE → SPACE** | Same order, our names, our DSP |
| Source | tape / poly sampler / live input | Synth engine, flash sample, or **TAPE = resample the FM-1's own output** | No audio input on the FM-1 (verify in Phase 0) |
| Granular | live time-warp + pitch-shift | Grain pool fed by a per-track ring buffer or TAPE | Shared pool with a hard cap |
| Filter | 48-band morphing resonator | SVF morph + **6-band tuned resonator** + formant + comb | 48 bands × 4 is out of budget |
| Colour | dual-band drive, crush, comp, noise | Drive, fold, crush, decimate, noise, simple comp | Single band first |
| Space | per-track reverb/delay, shimmer, freeze | **Shared** reverb + delay, per-track sends, FREEZE | One reverb, not four |
| Modulators | 4 per track | **2 MODS per track**, control rate | CPU, screen space |
| Performance | temp, macros, scenes | TEMP, 4 MACROS, 8 SCENES with morph | Fits 4 knobs + 16 white keys |

## 2. Hardware we're designing for

Known: 240×240 colour TFT; 4 knobs; function buttons HOME, SEQ, GLO, SAVE, LFO, EDIT, FX, SCL, ENV,
ARP, REC, OCT−, OCT+; 27 silicone keys (16 white = steps); speaker, battery, USB-C (MIDI + audio *to*
the host), TRS MIDI in, BLE MIDI; JieLi AC791N, single core by default (optional 2nd core exists, OFF).

**Measured in Phase 0 (`docs/HARDWARE.md` has the details and sources):**
- 44.1 kHz, ISR blocks of 128 frames, control blocks of 32 samples; 360 MHz, no FPU.
- **No audio input** of any kind: TAPE resamples the FM-1's own output only.
- 512 KiB internal SRAM, no PSRAM: 72 KiB RAM + 360 KiB pool, of which 41 KiB is free (33 KiB above the build's headroom). The base tree's
  delay (128 KiB), SLICER (32 KiB), engine state (4 × 12.6 KiB) and reverb/chorus (14 KiB) hold the rest.
- Flash: 3 user sample slots × 80 KiB (~7.4 s each at 22.05 kHz ADPCM); 126 KB of CC0 samples in the app image.

Buffer arithmetic (mono): 1 s at 44.1 kHz = 86 KiB at 16-bit (HI), 43 KiB at 8-bit µ-law (LO); at 22.05 kHz, half
that. **UMBRA's audio memory must replace the base tree's buffers, not add to them:** the delay's 128 KiB gives
~6 s of LO audio at 22.05 kHz shared across tracks, the SLICER's 32 KiB four short GRANULAR rings. Phase 1 sets
the split in `firmware/src/umbra_mem.h`.

## 3. The chain

Every device: bypassable, 2–3 pages of 4 params, a type selector, a visual, a declared CPU cost.
Tap a device's button to open it; tap again for the next page.

### 3.1 SOURCE — where the sound comes from
Types:
- **SYNTH** — one of the base tree's engines (FM6, ANALOG, NOISE, PHYS, DRUM, SLICE…), played by keys/sequencer.
- **SAMPLE** — flash slot (CC0 set or user slot), one-shot / loop / poly (≤4 voices).
- **TAPE** — loop recorder per track. Records from MIXER (master), another TRACK, or ITSELF (overdub). Length set by RAM; quality HI (16-bit) / LO (8-bit µ-law, 2× length). Plays as loop (varispeed, reverse) or POLY (chromatic over keys).
- **RETURN** — no sound of its own; takes another track's post-chain output. Turns a track into an effects return or a shadow of another.
- **OFF**

Pages: P1 `LEVEL · PITCH · START · LENGTH` · P2 `ATK · REL · LOOP · DIR` · P3 type-specific (engine macros for SYNTH; FROM/QUALITY/ARM for TAPE).

### 3.2 GRANULAR — granular
Reads TAPE if SOURCE is TAPE, else a short per-track ring buffer that always records SOURCE (target ≥ 0.5 s, sized in Phase 0).
- P1 `SIZE · DENSITY · PITCH · SPRAY` (spray = position jitter)
- P2 `SCAN · SPEED · CONTOUR · MIX` — SPEED 0 = suspended cloud; CONTOUR = window (hann, triangle, percussive, reverse-ramp)
- P3 `REVERSE % · PITCH RAND · SCALE LOCK · DETUNE` — scale lock uses the base tree's scale system
- Grain pool: global cap (start 12), per-track cap (start 6). Scheduled at control rate; per-sample work = 2 reads + linear interp + window table + gain. Fixed-point phase accumulators.

### 3.3 FILTER — filter & resonator
Types:
- **SVF** — LP / BP / HP / notch morph on one knob. Cheap.
- **RESONATOR** — 6 tuned 2-pole resonators set to a chord or the scale; MORPH spreads/collapses them. Drones out of noise.
- **FORMANT** — 3-band formant filter morphing through vowels; distant, choir-like.
- **COMB** — tuned comb with damping; metallic, bell-like when fed noise.

Pages: P1 `FREQ · RES · MORPH · MIX` · P2 `TUNE (chord/scale) · SPREAD · WANDER · KEYTRACK`. WANDER = slow built-in drift so static settings still move.

### 3.4 DRIVE — colour & damage
- P1 `DRIVE · FOLD · CRUSH · DECIMATE`
- P2 `NOISE · TILT · COMP · OUT`
- NOISE adds a hiss/crackle/tape-dust floor. Drive via tanh table, triangle fold, bit mask, sample-and-hold decimation in Hz. Saturate, never wrap.
- Stretch (budget permitting): split-band DRIVE with a 1-pole crossover.

### 3.5 SPACE — space
Shared reverb and delay buses on the master; per track you set sends.
- Per track (P1): `REV SEND · DLY SEND · PAN · WIDTH`
- Bus page (P2, hold the button): `DECAY · DAMP · DLY TIME (sync) · FEEDBACK`
- P3: `WOW · SHIMMER · PRE-DELAY · FREEZE`
- **FREEZE**: input closed, feedback held at unity — the tail hangs indefinitely. A first-class performance control.
- SHIMMER (pitch shift in feedback) is the most expensive item: its own flag, measured first.

## 4. MIXER — mixer & master
HOME → MIXER: 4 track levels on the knobs, white keys 1–4 mute, 5–8 solo. Master: one-knob DJ filter (LP ← centre → HP), glue compressor, soft limiter. Start from the base tree's mixer/GLO layer.

## 5. MODS — modulation
2 per track, control rate (once per block). Shapes: LFO (sine/tri/saw/square/S&H/drunk walk), ENV (note-triggered AD), FOLLOW (envelope follower on the track's own signal), RANDOM. Assign: hold LFO + turn a device knob = depth (bipolar). Reuse the base tree's LFO pages and mod matrix. Rates go very slow (minutes per cycle) — dark ambient lives there.

## 6. Performance
- **TEMP**: hold LFO + turn knobs → changes snap back on release.
- **MACROS**: 4 macros, each driving up to 4 params with ranges.
- **SCENES**: 8 snapshots of all device params on white keys 9–16 while holding ENV; knob 1 morphs current → target over a time knob (up to a minute).
- **CLEAR**: hold REC + OCT− on a track clears its TAPE and GRANULAR buffers (confirm first).

## 7. Controls (approved in Phase 0)

Panel: knobs MASTER, SELECT, PRESETS, ALGORITHM, KNOB 1–4; buttons FX SCL ENV LFO EDIT GLO / HOME SAVE ARP SEQ
PLAY REC; OCT− OCT+; 27 keys (16 white). Reference: `docs/controls.jpg`.

| Action | Control |
| --- | --- |
| Open SOURCE / GRANULAR / FILTER / DRIVE / SPACE | EDIT / ARP / SCL / FX / GLO (tap = open, again = next page) |
| Select track 1–4 | ALGORITHM knob, on every page |
| Change device type | PRESETS knob on a device page (SOURCE SYNTH: engine, then preset) |
| Bypass a device | hold its button + OCT+ (OCT− stays the quick layers' put-back; FX held + OCT− is still the octave) |
| MIXER | HOME (tap round to it); hold HOME = MENU |
| MODS / TEMP | LFO (pages); hold LFO + knob |
| MACROS & SCENES | ENV; hold ENV + white keys 9–16 = SCENES |
| Scale root | hold SCL + key (also tunes FILTER RESONATOR) |
| CLEAR | hold REC + OCT− (confirm first) |
| Tempo, volume | SELECT, MASTER |
| Transport, sequencer, record, save | PLAY, SEQ, REC, SAVE |

Phase 1 (interim, until each device's own DSP lands): SOURCE = the engine's pages + VOICE + ARP; GRANULAR and
FILTER = pass-through pages (FILTER's button keeps the scale and chord pages after it); DRIVE = DIST, SLICER, INSERT;
SPACE = the track's sends, the shared delay / reverb / chorus, then SONG. The footer's first row shows the chain strip
on every device page. Turning PRESETS to change a device's type arrives with the first device that has two types.

## 8. Screen & look (240×240)

Interactive reference: `docs/mockups/umbra-screen.html` (open in a browser) — layout and motion, not final art.

Two kinds of screen. **Device pages** give the whole middle of the display to one big visual for the
open device, with its 4 values in a strip above it — one picture per device, readable at arm's length.
**Home and mixer** show the eclipse, UMBRA's overall picture of the sound.

### 8.1 Device page
```
┌────────────────────────────┐
│ 1  2  3  4   SCENE 3  ♩ 62 │  track tabs (active inverted), scene, tempo
│ SRC GRN [FLT] DRV SPC      │  chain strip: open device boxed, bypassed hollow
├──────┬──────┬──────┬──────┤
│ FREQ │ RES  │ MRPH │ MIX  │  value strip — the same 4 params as the knobs,
│ 340Hz│  72% │  40% │ 100% │  in the same order, name + value
├──────┴──────┴──────┴──────┤
│                            │
│   |    |  ||   |     |     │  the device's own full-width visual
│   |    |  ||   |     |     │  (here: FILTER resonator bands)
│   |    |  ||   |     |     │
│                            │
│ RESONATOR · C MINOR        │  type + one line of context
└────────────────────────────┘
```

Each device's visual — original UMBRA drawings, sparse and dark, never copied from any other instrument:
- **SOURCE** — a dim waveform of the sample/TAPE buffer (or the engine's output scope for SYNTH) with a slow playhead; loop points as faint brackets; reverse shown by the playhead moving right-to-left.
- **GRANULAR** — the same waveform, very faint, with grains as small specks that appear where they read, drift upward and fade; SPEED 0 leaves them hanging in place.
- **FILTER** — thin vertical lines for the resonator/formant/comb bands at their frequencies, each glowing as it rings; SVF shows a single soft response curve instead.
- **DRIVE** — the transfer curve (input → output): straight when clean, bending with drive, folding back with FOLD, stair-stepping with CRUSH; NOISE as a fine grain over the whole field.
- **SPACE** — a tail of fading echoes trailing into darkness; delay taps as dim marks along it; with FREEZE on, the tail stops moving and holds.

### 8.2 Home and mixer — the eclipse
```
┌────────────────────────────┐
│ 1  2  3  4   SCENE 3  ♩ 62 │
│                            │
│            ████            │  black disc over a faint corona
│          ██░░░░██          │
│            ████            │
├──────┬──────┬──────┬──────┤
│ TRK1 │ TRK2 │ TRK3 │ TRK4 │  mixer levels on the knobs
│  -6dB│ -12dB│  MUTE│  -3dB│
└──────┴──────┴──────┴──────┘
```
- The disc's coverage follows total output level: silence = a thin crescent, full = totality (smoothed ~1 s).
- The corona brightens and frays with the overall DRIVE amount across tracks.
- **FREEZE stops all motion** on every screen until released.
- Boot: black screen, a crescent slowly closes to totality, the name fades in beneath. No sound.

### 8.3 Motion rules (all screens)
Slow and sparse; nothing blinks or flashes. Visuals read values the audio side publishes once per
block; draw time is capped and frames are skipped under CPU load — the audio always wins.

Palettes (all must pass the UI test): **TOTALITY** (black, cold grey corona — default), **ASH** (charcoal and bone), **BLOODMOON** (black and deep rust red), **NOCTILUCENT** (black and pale electric blue).

**UI sounds: none.** Menus are silent; there is no boot tune. The only sound UMBRA makes is the music.

## 9. Factory projects (sound recipes)
1. **Dead Bells** — FM6 bell → GRANULAR slow scan, long grains → FILTER RESONATOR minor chord → SPACE FREEZE on a SCENE.
2. **Distant Choir** — NOISE → FILTER FORMANT vowel morph via drunk MOD → long reverb.
3. **Sunken Pulse** — ANALOG saw → DRIVE fold + crush → FILTER SVF low-pass, keytracked.
4. **Worn Cathedral** — TAPE resample of MIXER, LO quality → GRANULAR suspended, pitch rand scale-locked.
5. **Shadow** — Track 4 = RETURN of track 1, GRANULAR reverse 60 % + COMB → a ghost of track 1.
6. **Iron Rain** — DRUM → COMB tuned to scale → delay synced dotted-8th.

## 10. Budget plan
- Target: 4 tracks, full chain at defaults, ≤ 70 % of one core. 2nd core stays optional.
- Every device declares its cost in `tests/target_budget.txt`; Phase 0 fills in measured numbers.
- Over-budget behaviour is designed in: grain cap, voice stealing, RESONATOR drops 6 → 4 bands, SHIMMER off.
- RAM: all audio buffers static, in one header, checked against `tests/ram_budget.txt`.

## 11. Saved data
PROJECT format (`UMBRA_PROJ v1`): per track — source type + params, device bypass/types/params, mods, sends; per project — macros, scenes, mixer, sequences. TAPE audio is **not** saved in v1 (RAM only); flash-saved TAPE is a v2 feature with its own version bump.

## 12. Decisions and open questions
- **Decided (Phase 0):** stay on the current base tree. FM1 Open's engine is a closed, FM-1-only binary SDK
  (`docs/HARDWARE.md`). The metronome click stays (a music tool, off by default; not a UI sound).
- **Open:** keep the base tree's sequencer as-is, or go "tape-first"?
