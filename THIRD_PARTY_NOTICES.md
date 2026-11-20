# Third-party code

Everything needed to build this project is vendored in this repository —
cloning it is enough, nothing else needs to be downloaded. The pieces below
are copied in unchanged (their own license file sits alongside them); only
the engine itself (`source/`, minus `third_party/`) was written or modified
for this project.

| Path | What it is | License | Origin |
|---|---|---|---|
| `source/` (minus `third_party/`) | The decomp/recomp engine, trimmed to just what Comix Zone needs (no Streets of Rage-specific menu, outfits, cheats, fighter select or launcher) | MIT | Originally built for the [Streets of Rage Collection Recomp](https://github.com/OriginalSwayze/Streets-of-Rage-Collection-Recomp) project; see `LICENSE` |
| `source/third_party/sdl2` | SDL2 | zlib | https://github.com/libsdl-org/SDL |
| `source/third_party/musashi` | Musashi, the 68000 interpreter used as a fallback for the ~3-5% of code not yet discovered statically | MIT-style (see `m68k.h`) | https://github.com/kstenerud/Musashi |
| `source/third_party/z80` | Z80 interpreter (the sound CPU is always interpreted, never recompiled) | MIT | https://github.com/superzazu/z80 |
| `source/third_party/ymfm` | YM2612 FM synthesis chip emulation | BSD-3-Clause | https://github.com/aaronsgiles/ymfm |
| `source/third_party/single/dr_*.h` | Audio file decoders (replacement music) | public domain / MIT-0 | https://github.com/mackron/dr_libs |
| `source/third_party/single/stb_*` | Font rendering and audio decoding | public domain / MIT | https://github.com/nothings/stb |
| `decomp/sega2asm/` | Generic Sega Genesis/Mega Drive disassembler, used to read the ROM statically for `decomp/` | MIT | https://github.com/hansbonini/sega2asm |
