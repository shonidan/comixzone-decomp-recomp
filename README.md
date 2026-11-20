# Comix Zone — native recompilation + static decompilation

A native (no emulator, no Wine) recompilation of Comix Zone (Sega Genesis /
Mega Drive, 1995), plus a static decompilation project reading the same ROM
directly.

**This repo contains no ROM data.** You need to supply your own legally
obtained copy of the US cartridge ROM (verified SHA-1:
`e8747eefdf61172be9da8787ba5be447ec73180f`) as `rom/ComixZone_U.bin`.

## Status

~95-97% native, playable start to finish, verified block by block against
an interpreter. One sound bug — all audio permanently freezes about 59
seconds into any playthrough — was root-caused and fixed with a 2-instruction
ROM patch; see `decomp/NOTES.md` for the full, detailed writeup (several
sessions' worth of instrumentation, false starts, and the eventual fix), and
`patches/sound-freeze-fix.ips` to apply it yourself.

## Structure

- `decomp/` — static decompilation project, reading the ROM directly, no
  emulation. `sega2asm/` (vendored, see below) is the generic Genesis
  disassembler it's built on. `comixzone.yaml` maps the segments mapped so
  far (header, the Z80 sound driver). `NOTES.md` is the detailed research
  log — start with its first "Resumen ejecutivo" section.
- `source/` — the playable native recompilation: a self-contained copy of
  the generic decomp/recomp engine (originally built for the Streets of
  Rage Collection, see `LICENSE`), trimmed to just what Comix Zone needs
  to build and run — no Streets of Rage-specific menu, fighter outfits,
  cheats or launcher code, plus its own third-party dependencies vendored
  in `source/third_party/` (nothing to download separately). See
  `source/README.md` to build and run it.
- `patches/sound-freeze-fix.ips` — standard IPS patch fixing the sound
  freeze described above. Apply it to your own ROM with any IPS patcher
  (e.g. Lunar IPS, flips) before building or playing.
- `rom/` — not committed; put your own ROM dump here.
- `THIRD_PARTY_NOTICES.md` — where every vendored piece of code comes
  from and under what license.

## Reproducing the decompilation output

`decomp/out/` (the disassembly `sega2asm` produces) isn't committed, since
it's derived from the ROM — regenerate it locally once you have the ROM:

```bash
cd decomp
go run ./sega2asm --help    # see sega2asm/README.md for the exact invocation
```

## Building the playable recompilation

```bash
cd source
node tools/setup.mjs --game comixzone "/path/to/your/ComixZone_U.bin"
cmake -B build -S . -DGAME=comixzone
cmake --build build -j
./build/ComixZone "/path/to/your/ComixZone_U.bin"
```

See `source/README.md` for details and build flags.
