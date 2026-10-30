# JieLi AC79 SDK files

The three files of JieLi's AC79NN_SDK_V1.2.1 (2023-12-13, <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>) that the
FM-1 package needs, laid out as in the SDK, so a build needs no SDK checkout (`AC79_SDK=sdk/ac79 ./build.sh`; CI does
this). Apache-2.0: `LICENSES/Apache-2.0.txt`. `tools/build.py` checks each against its SHA-256 before use.

| File | SHA-256 |
| --- | --- |
| `ac79/cpu/wl82/tools/uboot.boot` | `4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4` |
| `ac79/cpu/wl82/tools/cfg_tool.bin` | `276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b` |
| `ac79/cpu/wl82/tools/cfg/eq_cfg_hw.bin` | `41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d` |
