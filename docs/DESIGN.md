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

As built (Phase 4b, `firmware/src/umbra_source.c`, `umbra_tape.c`): the type is picked with PRESETS on the **SOURCE**
page (EDIT's cycle, after ARP 2); the engine pages and VOICE show only for SYNTH and SAMPLE. SAMPLE loads the SAMPLE
engine (its flash sets and user slots) and then plays as SYNTH. OFF is silence (the engine keeps its notes, so switching
back is instant). **RETURN** (`FROM · LVL`) reads the other track's block after its chain, one block late (0.7 ms) on
one core or two. **TAPE** pages `LVL · PTCH · STRT · LEN` / `ATK · REL · MODE · DIR` / `FROM · QUAL · REC · FDBK`:
- Memory: 38 KiB shared by the TAPE tracks in equal parts (one: all of it), at 22.05 kHz: HI 16-bit 0.88 s, LO µ-law
  1.76 s for one track. Changing the parts or the quality empties the takes. Never saved (RAM only).
- FROM MIX (the mix before the limiter), T1–T4 (a track after its chain) or SELF (its engine, monitored; REC over a
  take overdubs where it plays, the old take × FDBK).
- REC: a new take from the start until OFF or full; full, it plays with REC still on. **CLR** asks first, then empties
  the take and the track's GRANULAR ring, a 2 KiB chunk a block (no CPU spike).
- MODE LOOP: START .. START+LEN round at PITCH, forward or reversed, the ends faded; KEYS: the track's notes play it
  chromatically from START (C4 = PITCH), 4 voices, ATK / REL.
- GRANULAR on a TAPE track in LO reads the take itself (all of it valid: the cloud can sit anywhere in it); in HI, or
  while a take is still recording, its own ring.
Host cost: a LOOP 0.49 ANALOG voices, KEYS with 4 voices 0.87 (`tests/tape_test.c`).

### 3.2 GRANULAR — granular
Reads TAPE if SOURCE is TAPE, else a short per-track ring buffer that always records SOURCE (target ≥ 0.5 s, sized in Phase 0).
- P1 `SIZE · DENSITY · PITCH · SPRAY` (spray = position jitter)
- P2 `SCAN · SPEED · CONTOUR · MIX` — SPEED 0 = suspended cloud; CONTOUR = window (hann, triangle, percussive, reverse-ramp)
- P3 `REVERSE % · PITCH RAND · SCALE LOCK · DETUNE` — scale lock uses the base tree's scale system
- Grain pool: global cap (start 12), per-track cap (start 6). Scheduled at control rate; per-sample work = 2 reads + linear interp + window table + gain. Fixed-point phase accumulators.

As built (Phase 4a, `firmware/src/umbra_granular.c`): type **GRAINS** beside THRU (PRESETS on ARP's page). Pages
`SIZE · DENS · PTCH · SPRY` / `SCAN · SPED · CONT · MIX` / `REV · PRND · SCAL · DTUN`. The ring: 0.51 s a track at
22.05 kHz, 8-bit µ-law (11 KiB each, 44 KiB: from the delay's freed half). SPEED 64 follows live; lower or higher the
read point drifts back or catches up through the ring; 0 suspends recording and the cloud hangs. 12 grains (8 above
~80 % CPU), 6 a track; a grain with no slot fades the oldest out over 64 samples. SCALE puts each grain on the track's
scale and lights its keys. Host cost: the full pool with four rings ≈ 4 ANALOG voices, a grain ≈ 0.2. A grain waits
until the ring holds what it reads (an empty ring's bytes would decode as full-scale negative). On a TAPE track in LO it
reads the take (§3.1).

### 3.3 FILTER — filter & resonator
Types:
- **SVF** — LP / BP / HP / notch morph on one knob. Cheap.
- **RESONATOR** — 6 tuned 2-pole resonators set to a chord or the scale; MORPH spreads/collapses them. Drones out of noise.
- **FORMANT** — 3-band formant filter morphing through vowels; distant, choir-like.
- **COMB** — tuned comb with damping; metallic, bell-like when fed noise.

Pages: P1 `FREQ · RES · MORPH · MIX` · P2 `TUNE (chord/scale) · SPREAD · WANDER · KEYTRACK`. WANDER = slow built-in drift so static settings still move.

As built (Phase 2, `firmware/src/umbra_filter.c`; types in this stored order: THRU, SVF, COMB, RESONATOR, FORMANT), each
type's two pages keep the shape above with its own words:
- SVF `FREQ · RES · MRPH · MIX` / `KTRK · WNDR` — MRPH sweeps LP → BP → HP → NOTCH.
- COMB `NOTE · FDBK · DAMP · MIX` / `KTRK · WNDR · POL` — the delay tuned to a note (F#1..C7), POL + / − (− an octave down, hollow).
- RESONATOR `ROOT · RING · MRPH · MIX` / `TUNE · SPRD · WNDR · KTRK` — TUNE: MINOR, MAJOR, SUS4, FIFTHS, OCTAVE, SCALE (the
  song's scale from ROOT); MRPH collapses the six bands to the root or spreads them; drops to 4 bands above ~80 % CPU.
- FORMANT `SHFT · RES · VOWL · MIX` / `KTRK · WNDR` — three bands through A E I O U.

### 3.4 DRIVE — colour & damage
- P1 `DRIVE · FOLD · CRUSH · DECIMATE`
- P2 `NOISE · TILT · COMP · OUT`
- NOISE adds a hiss/crackle/tape-dust floor. Drive via tanh table, triangle fold, bit mask, sample-and-hold decimation in Hz. Saturate, never wrap.
- Stretch (budget permitting): split-band DRIVE with a 1-pole crossover.

As built (Phase 2, `firmware/src/umbra_drive.c`): type **DAMAGE**, beside **BASIC** (the base tree's DIST, SLICER and
INSERT pages, kept). Page labels `DRV · FOLD · CRSH · DCM` / `NOIS · TILT · COMP · OUT`; OUT in dB (OFF .. +7.5, 0 dB
default). At its defaults DAMAGE is the input bit for bit.

### 3.5 SPACE — space
Shared reverb and delay buses on the master; per track you set sends.
- Per track (P1): `REV SEND · DLY SEND · PAN · WIDTH`
- Bus page (P2, hold the button): `DECAY · DAMP · DLY TIME (sync) · FEEDBACK`
- P3: `WOW · SHIMMER · PRE-DELAY · FREEZE`
- **FREEZE**: input closed, feedback held at unity — the tail hangs indefinitely. A first-class performance control.
- SHIMMER (pitch shift in feedback) is the most expensive item: its own flag, measured first.

As built (Phase 3, `firmware/src/umbra_space.c`): GLO opens **SPACE** (`REV · DLY · PAN · WIDTH`; WIDTH is the track's
SPREAD), **SPACE 2** (`SIZE · DAMP · TIME · FDBK`, the base tree's reverb and delay values), **SPACE 3**
(`WOW · SHIM · PRE · FRZ`), then SENDS (with the chorus send) and the bus pages as before. P2 is the next page, not
a hold. WOW wavers the delay (0.55 Hz and 1.65 Hz, up to ~5 ms); PRE-DLY delays the reverb's send 0–100 ms;
FREEZE closes both sends and holds both loops at unity, gliding in and out over ~17 ms. HALL and ROOM hang (HALL's
taps stop drifting so nothing dulls); SPRING only closes its input. SHIMMER is built only with `UMBRA_SHIMMER=1`
(off by default; host cost about 0.4 of an ANALOG voice). The song's values are saved (UMB1 v3); FREEZE never is.

## 4. MIXER — mixer & master
HOME → MIXER: 4 track levels on the knobs, white keys 1–4 mute, 5–8 solo. Master: one-knob DJ filter (LP ← centre → HP), glue compressor, soft limiter. Start from the base tree's mixer/GLO layer.

As built (Phase 3): MIXER keeps the base tree's knobs (the selected track's LEVEL, PAN, REV, MUTE, with all four
tracks' cards). The four levels on the knobs are the HOME screen's. On MIXER, white keys 1–4 mute tracks 1–4 and
5–8 solo them; the keys are silent there and lit while on. Solo is not saved. HOME's round is MIXER → CLOCK →
**MASTER** (`DJ · GLUE`, `firmware/src/umbra_master.c`): DJ low-pass 16 kHz → ~110 Hz below 0, high-pass
30 Hz → ~4 kHz above. GLUE is a linked compressor (−3 to −11 dBFS, 2:1 to 4:1). Both sit before the base tree's
limiter and soft clip.

## 5. MODS — modulation
2 per track, control rate (once per block). Shapes: LFO (sine/tri/saw/square/S&H/drunk walk), ENV (note-triggered AD), FOLLOW (envelope follower on the track's own signal), RANDOM. Assign: hold LFO + turn a device knob = depth (bipolar). Reuse the base tree's LFO pages and mod matrix. Rates go very slow (minutes per cycle) — dark ambient lives there.

As built (Phase 5a, `firmware/src/umbra_mod.c`, kept simple at Shane's request; more shapes and gestures when needed):
two MODS a track on pages **MOD 1** / **MOD 2** (the end of LFO's cycle, after the base tree's MOD matrix): `SHAP · RATE · DEST · DPTH`.
- SHAPE SINE TRI SAW SQR S&H DRIFT (S&H: a new random value each cycle; DRIFT: glides to it). ENV and FOLLOW: later.
- RATE: a cycle from 10 minutes (0) down to 0.1 s (127); 64 is ~7.4 s.
- DEST: OFF or a value of the track's SOURCE, GRANULAR, FILTER or DRIVE as its type has it, shown by name and the
  device's letter (`FREQ F`); the knob passes over what the devices lack and over choices (MODE, REC...). A type
  change leaves a DEST the new type lacks doing nothing.
- DEPTH -64..63: at 63 the value swings by its whole range either way (clamped); negative: upside down.
- Assigning is DEST on the page; hold LFO + knob stays the base tree's TEMP (momentary). Per block, in the audio ISR
  like the base tree's matrix: the stored values are put back after each block, so the UI and saves never see a
  moved value. Nothing set: bit-identical. `UMBRA_MOD=0` keeps the pages, moves nothing. Saved: UMB1 v5.

## 6. Performance
- **TEMP**: hold LFO + turn knobs → changes snap back on release.
- **MACROS**: 4 macros, each driving up to 4 params with ranges.
- **SCENES**: 8 snapshots of all device params on white keys 9–16 while holding ENV; knob 1 morphs current → target over a time knob (up to a minute).
- **CLEAR**: hold REC + OCT− on a track clears its TAPE and GRANULAR buffers (confirm first). As built (4b): TAPE's REC turned to
  **CLR**, then a confirm (REC + OCT− is the base tree's recording gesture, kept).

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
on every device page.

Lights (Phase 3): the keys have one colour and four levels (dark, the backlight glow, a mid level, lit) plus a
breath. With MENU > LEDS DIM HI or DIM LO every button and key that is not lit or breathing glows, on every page,
so the panel can be found in the dark. Over that glow:
- MIXER: white keys 1–8 at the mid level; a muted track's key lit; a soloed track's key breathes.
- FILTER (RESONATOR, COMB): the keys of the notes it is tuned to at the mid level (the chord as MORPH sets it).
- SOURCE pages, and anywhere with MENU > SCALE LEDS ON: the scale's keys at the mid level, the root's lit.
- GLO held + G5: FREEZE on / off (SPACE 3's), G5 lit while frozen.
Pages trimmed so each value has one home: DRIVE BASIC's FX page is DIST alone, SENDS is CHOR alone, DLY is COLOR
and MIX, REVERB is TYPE (the sends, PAN, TIME, FDBK, SIZE and DAMP are SPACE's pages).

Phase 2: on a FILTER or DRIVE page, PRESETS picks the device's type (its values go to the type's defaults); hold the
device's button + OCT+ to bypass it (the chain strip outlines a bypassed device). Device values are not automatable
or MOD targets until Phase 5.

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

Until factory projects exist (Phase 6), recipes are in the browser emulator's demo list (`web/emu/umbra_recipes.c`;
golden renders `tests/umbra_golden.txt`). Phase 2 has Sunken Pulse (plus the kit through a RESONATOR in fifths) and
Distant Choir; Distant Choir's vowel drift is FORMANT's WANDER until the drunk MOD arrives (Phase 5). Phase 3 adds
Frozen Hall: the choir and a pad into the longest HALL with PRE-DLY and WOW, for FREEZE. Phase 4a adds Dead Bells
(FM6 BELL → GRAINS, long grains, slow scan → RESONATOR A minor → HALL; FREEZE by GLO + G5 until SCENES). Phase 4b
adds Worn Cathedral (an FM6 pad; track 2's TAPE takes the mix in LO until full, then GRAINS hangs over the take, SPEED
0, random pitches on C minor, into a long HALL) and Shadow (an ANALOG pluck; track 4 a RETURN of it through GRAINS
60 % reversed and a COMB). Phase 5a adds Low Tide (an FM6 pad through a resonant SVF, MOD 1 sweeping its FREQ, MOD 2
drifting its RES).

## 10. Budget plan
- Target: 4 tracks, full chain at defaults, ≤ 70 % of one core. 2nd core stays optional.
- Every device declares its cost in `tests/target_budget.txt`; Phase 0 fills in measured numbers.
- Over-budget behaviour is designed in: grain cap, voice stealing, RESONATOR drops 6 → 4 bands, SHIMMER off.
- RAM: all audio buffers static, in one header, checked against `tests/ram_budget.txt`.

## 11. Saved data
A project is the base tree's record (one full 4 KB flash sector: no room) plus UMBRA's own record beside it, in
its own storage object ("UMB1", `firmware/src/umbra_store.c`, flash 0xE8000..0xF1FFF: four project slots and the
autosave, A/B each). Version 1 (Phase 1): per track, each device's type and bypass. Version 2 (Phase 2) adds each device's eight values
(88 bytes a track; a v1 record loads with the types' defaults). Version 3 (Phase 3) adds the song's SPACE and MASTER
values (16 bytes; FREEZE stored as off; a v2 record loads with their defaults). Version 4 (Phase 4) stores 12 values
a device (GRANULAR's three pages): 128 bytes a track, 540 in all; v1–v3 load. Version 5 (Phase 5a) adds each
track's two MODS (4 values each, 32 bytes: 572 in all); v1–v4 load with the MODS off. Later versions add
sends, macros and scenes (more objects per project if needed: 40 KiB of FL_XDATA1 is still free). The UMBRA
record names the project record it was saved with (its CRC): a base-tree project, a slot re-saved by the base tree,
or a torn write loads with the default chain, never another project's. TAPE audio is **not** saved (RAM only);
flash-saved TAPE is a later version with its own bump.

## 12. Decisions and open questions
- **Decided (Phase 0):** stay on the current base tree. FM1 Open's engine is a closed, FM-1-only binary SDK
  (`docs/HARDWARE.md`). The metronome click stays (a music tool, off by default; not a UI sound).
- **Open:** keep the base tree's sequencer as-is, or go "tape-first"?
