# Comix Zone — native recompilation + static decompilation

A native (no emulator, no Wine) recompilation of Comix Zone (Sega Genesis /
Mega Drive, 1995), plus a static decompilation project reading the same ROM
directly.

**This repo contains no ROM data.** You need to supply your own legally
obtained copy of the US cartridge ROM (verified SHA-1:
`e8747eefdf61172be9da8787ba5be447ec73180f`) as `rom/ComixZone_U.bin`.

## Status

~95-97% native, playable start to finish, verified block by block against
an interpreter. The sound bug that used to freeze all audio ~59 seconds into
any playthrough was root-caused and fixed with a 2-instruction ROM patch;
see `decomp/NOTES.md` for the full, detailed writeup (several sessions'
worth of instrumentation, false starts, and the eventual fix), and
`patches/sound-freeze-fix.ips` to apply it yourself.

A small pause menu (Esc or the pad's Select/"-" button) has save states,
rewind/fast-forward, and a Cheats page: Infinite health, Infinite lives
(no known address yet, so this one is a no-op), 1-hit kill enemies,
Multi-jump, and Jump to level (resets into one of 6 scenes — not confirmed
to be in story order).

One known, minor issue remains open: native and interpreted execution
start to disagree from frame 1661 of a fixed-seed playthrough on, likely a
small timing drift rather than a mistranslated instruction (every
interrupt taken before that point matches exactly, in order, level and
PC). Not observed to affect actual play; see `decomp/NOTES.md`'s most
recent session for the investigation and where it left off.

## Structure

- `decomp/` — static decompilation project, reading the ROM directly, no
  emulation. `sega2asm/` (vendored, see below) is the generic Genesis
  disassembler it's built on. `comixzone.yaml` maps the segments mapped so
  far (header, the Z80 sound driver). `NOTES.md` is the detailed research
  log — start with its first "Resumen ejecutivo" section.
- `source/` — the playable native recompilation: a self-contained copy of
  the generic decomp/recomp engine (originally built for the Streets of
  Rage Collection, see `LICENSE`), trimmed to just what Comix Zone needs
  to build and run — no Streets of Rage's own tabbed menu, fighter
  outfits or launcher code (its own small pause menu and Cheats page are
  real, see Status above), plus its own third-party dependencies vendored
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
node tools/kit.mjs comixzone    # disassembles, configures, builds, verifies, writes a report
./build/comixzone/ComixZone "/path/to/your/ComixZone_U.bin"
```

Or, once built, just use `./run.sh` at the repo root (looks for the ROM at
`rom/ComixZone_U.bin`, or takes a path as its one argument).

See `source/README.md` for details and build flags.
