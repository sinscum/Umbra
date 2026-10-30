# UMBRA hardware facts (FM-1)

Written in Phase 0 from the base tree's code (Felucca v1.7.0, commit 65b21d3), a measuring device build in CI
(`.github/workflows/device-check.yml`) and M-VAVE's FM1 Open V14. **[unconfirmed]** marks a figure that needs the device.

## Chip and clock
- JieLi AC791N (WL82), pi32v2, no FPU. Code executes in place from flash at 0x02000120.
- System clock 360 MHz (PLL1 540 MHz × 2/3, `firmware/bsp/chip/fm1_clock.h`): ~8,160 cycles per output sample at 44.1 kHz.
- Second core exists (`UMBRA_CPU2`, built in, OFF by default: MENU > SYSTEM > 2ND CORE, beta).

## Audio
| | Value | Source |
| --- | --- | --- |
| Sample rate | **44,100 Hz** (`FS`); the I2S clock actually runs ~44,117.6 Hz | `build/gen/umbra_tables.h`, `audio.c` DAC_TICKS |
| ISR block | **128 frames** (half buffer, 2.9 ms) | `core.h` HALF_FRAMES |
| Control block | **32 samples** (`CTL`), 4 per ISR block | Felucca-Engines tables |
| Output | stereo Q15 to the DAC (headphones / line, speaker); USB audio *to* the host 16-bit stereo 44.1 / 48 kHz | `umbra.c` UMBRA_UAC, `bsp/chip/usb.c` |
| Load shedding | above 85 % of a block the audio side sheds work | `audio.c` SHED_TICKS |
| Audio **input** | **None.** The SAR ADC reads only the MASTER pot, the battery and two key-matrix rows; USB audio is output-only (UAC "input" = the FM-1 as a microphone for the host). No mic/line codec path in the BSP. | `bsp/boards/fm1/fm1_adc.h`, `usb.c` |

So TAPE can only resample the FM-1's own output (as DESIGN assumes).

## RAM (internal SRAM only — no PSRAM)
The linker map (`firmware/app.ld`) covers 0x01C00000..0x01C80000 = **512 KiB**, all internal; nothing maps external RAM.

| Region | Size | Use |
| --- | --- | --- |
| RAMTEXT | 24 KiB | code that runs while flash is busy; 2nd core's idle loop |
| RAM | **72 KiB** | .data + .bss: what the audio/key ISRs touch |
| POOL | **360 KiB** | big zero-initialised buffers |
| NOINIT | ~15 KiB | survives reset: crash context, stacks |

Measured (device build of the unchanged base tree, CI `device-check`, 2026-10-30): .data+.bss **61,844 of 73,728 B**
(11.6 KiB free), pool **326,208 of 368,640 B** (**41.4 KiB free**). The build enforces ≥ 4 KiB RAM and ≥ 8 KiB pool
headroom, so ~33 KiB of pool is usable for new buffers. `./build.sh` prints these on every build.

Biggest pool users (from source):
| Buffer | Bytes | Notes |
| --- | --- | --- |
| delay `dly_buf` | 131,072 | 65,536 × int16 = 1.49 s at 44.1 kHz |
| engine state `eng_mem` | 4 × 12,888 = 51,552 | per part (13,344 with ADPCM) |
| SLICER `sl_buf` | 4 × 8,192 = 32,768 | 186 ms per track at 22.05 kHz |
| reverb combs `rev_comb` | 9,874 | |
| chorus `cho_buf` | 4,096 | |
| UI canvases, undo, project/storage work buffers, FM6 bank | the rest | main loop only |

**What this means for UMBRA:** there is no spare half-megabyte. Every TAPE / GRANULAR / SPACE buffer has to come
from replacing these. Examples at 16-bit / 8-bit µ-law:
- the delay's 128 KiB = 1.49 s HI or 2.97 s LO at 44.1 kHz; **2.97 s HI / 5.9 s LO at 22.05 kHz** — shared by all four tracks;
- the SLICER's 32 KiB = 4 × 0.37 s LO at 22.05 kHz — enough for the GRANULAR ring buffers (DESIGN asks ≥ 0.5 s: needs ~44 KiB at 22.05 kHz LO for four).

DESIGN §2's "4 × TAPE buffers" does not fit as written; Phase 1/4 have to choose (one shared TAPE, or short per-track ones).

## Flash (1 MiB NOR, offsets from flash 0)
| Range | Size | Use |
| --- | --- | --- |
| 0x000000..0x093000 | 588 KiB | boot + app area; app slot 0x8DFBC = 581,564 B; app image **513,532 B** (66 KiB free, measured) |
| 0x093000..0x097000 | 16 KiB | FL_XDATA0: free for firmware data |
| 0x097000..0x0E0000 | 292 KiB | store: settings, projects, user presets; **user sample slots** at 0xA0000 |
| 0x0E0000..0x0E5000 | 20 KiB | update loader staging |
| 0x0E5000..0x0E7000 | 8 KiB | autosave A/B |
| 0x0E7000..0x0FC000 | 84 KiB | FL_XDATA1: FM6 voice bank (0xE7000) + firmware data |
| 0x0FC000..0x0FF000 | 12 KiB | globals |
| 0x0FF000 | 4 KiB | key / MAC |

The CC0 sample sets are in the app image (126 KB of ADPCM, `gen_samples.py`) and can be dropped or replaced to make room.

## How audio gets in today
- **SAMPLE**: IMA ADPCM sets compiled into the app (5 sets, 11 zones), each zone at its native rate, played by a Q16 phase step; loops keep the ADPCM state.
- **User slots**: **3 slots × 80 KiB** at 0xA0000 (`eng_sample.c` SMP_USER_*), ADPCM uploaded by the web editor at 22.05 kHz: ~7.4 s per slot. Played straight from flash (XIP), no RAM copy.
- **SLICE**: slices a user slot or a built-in break; manual slice points saved in the slot's last sector (`slice_store.c`).
- **SLICER** (FX): records 186 ms per track into the pool and replays patterns.
- For comparison, FM1 Open V14 has 8 slots sharing ~19.55 s.

## CPU
`tests/cpu_baseline.txt` counts **host** instructions per sample (cc -O2), not FM-1 load, so it ranks engines but
gives no FM-1 %. The device shows its real load in MENU > INFO and on the console (`cpu_pct`). Average per preset,
host instructions per sample (lower is cheaper):

| Engine | Avg | | Engine | Avg |
| --- | --- | --- | --- | --- |
| LOFI | 637 | | PHYS | 1113 |
| SAMPLE | 656 | | PHASE | 1160 |
| GRAIN | 818 | | DRUM | 1175 |
| VOICE | 965 | | ANALOG | 1192 |
| SLICE | 995 | | FM6 | 1210 |
| WHEEL | 1068 | | NOISE | 1265 |
| | | | TRIO | 1311 |

`tests/target_budget.txt` holds static loop costs from the device disassembly (e.g. `grain_render` 1731, `analog_render` 585).
**[unconfirmed: FM-1 % per engine at 4 voices — needs the device, or a calibrated factor from one device measurement]**

## UI sounds
None at boot or in menus. The only extra sound is the metronome (`click.c`: MENU > AUDIO CLICK, OFF by default, and COUNT-IN), added only to the DAC.

## FM1 Open (m-vave/fm1_open, V14, 2026-10-21)
- APP sources GPL-3.0-only + a linking exception for M-VAVE's **closed binary engine SDK** (FM1-only grant). Windows-only build (JieLi toolchain 2.5.3).
- Its boot/update files and the **stock recovery image `FM-1_015.fwsc` are Apache-2.0** — UMBRA could ship or link the recovery image.
- Hardware facts it adds: DIN, USB and BLE MIDI in; USB/BLE out; MASTER is an ADC pot; 8 sample slots / ~19.55 s.
