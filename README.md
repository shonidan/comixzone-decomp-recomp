# Comix Zone — native recompilation + static decompilation

A native (no emulator, no Wine) recompilation of Comix Zone (Sega Genesis /
Mega Drive, 1995), plus a static decompilation project reading the same ROM
directly. Part of [GenesisRecomps](../).

**This repo contains no ROM data.** You need to supply your own legally
obtained copy of the US cartridge ROM (verified SHA-1:
`e8747eefdf61172be9da8787ba5be447ec73180f`) as `rom/ComixZone_U.bin`.

## Status

~95-97% native, jugable de punta a punta, verified block by block against
an interpreter. One sound bug — all audio permanently freezes about 59
seconds into any playthrough — was root-caused and fixed with a 2-instruction
ROM patch; see `decomp/NOTES.md` for the full, detailed writeup (several
sessions' worth of instrumentation, false starts, and the eventual fix), and
`patches/sound-freeze-fix.ips` to apply it yourself.

## Structure

- `decomp/` — static decompilation project (uses `../Tools/sega2asm`,
  reading the ROM directly, no emulation). `comixzone.yaml` maps the two
  segments mapped so far (header, the Z80 sound driver). `NOTES.md` is the
  detailed research log — start with its first "Resumen ejecutivo" section.
- `source/` — the playable native recompilation, built with
  `../Tools/Streets-of-Rage-Collection-Recomp` (a shared engine, also used
  by the Streets of Rage games). See `source/README.md` to build and run it.
- `patches/sound-freeze-fix.ips` — standard IPS patch fixing the sound
  freeze described above. Apply it to your own ROM with any IPS patcher
  (e.g. Lunar IPS, flips) before building or playing.
- `rom/` — not committed; put your own ROM dump here.

## Reproducing the decompilation output

`decomp/out/` (the disassembly `sega2asm` produces) isn't committed either
— regenerate it locally once you have the ROM:

```bash
cd decomp
# see ../../Tools/sega2asm/README.md for exact invocation
```

## Building the playable recompilation

```bash
cd ../Tools/Streets-of-Rage-Collection-Recomp
node tools/kit.mjs comixzone
cp build/comixzone/ComixZone ../../ComixZone/source/build/
```

See `source/README.md` for details, and `Tools/Streets-of-Rage-Collection-Recomp/COMIXZONE_STATUS.md`
for that engine's own session history with this game.
