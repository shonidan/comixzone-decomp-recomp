# Comix Zone — recompilación nativa jugable

Este directorio es autocontenido: tiene su propia copia (recortada) del
motor de decomp/recomp, sin depender de ningún otro repositorio del lado.

## Preparar y compilar

Todo lo necesario (SDL2, el intérprete 68000, etc.) ya está adentro de
`third_party/` — no hace falta descargar nada más, ni inicializar
submódulos.

```bash
node tools/setup.mjs --game comixzone "/ruta/a/tu/ComixZone_U.bin"
cmake -B build/comixzone -S . -DGAME=comixzone
cmake --build build/comixzone -j
```

Importante: compilar en `build/comixzone` (no en `build` a secas) — las
herramientas de cobertura/recompilación (`disasm.mjs`, `recomp.mjs`,
`save-coverage.mjs`) esperan el build y su `coverage.bin` ahí
(`BUILD_DIR = build/<game>`, ver `tools/lib/game.mjs`), porque el
ejecutable escribe `coverage.bin` al lado de sí mismo. Si compilás en
otro lado, esas herramientas nunca van a ver lo que jugaste. Para no
pensarlo, usar directamente `node tools/kit.mjs comixzone`, que compila
ahí, juega headless para encontrar código nuevo, recompila y verifica
todo en un solo paso.

## Jugar

```bash
./build/comixzone/ComixZone "/ruta/a/tu/ComixZone_U.bin"
```

`--headless N` corre N frames sin ventana; `--press FRAME:BOTONES:LEN`
para scripts de entrada; ver `runtime/src/main.c` para el resto de flags.

## Qué es esto

Un ejecutable nativo (ELF x86-64, ~6MB) compilado a partir del ROM de
Comix Zone, sin Wine ni emulador — el código del 68000 original está
recompilado a C y compilado para la máquina, con un intérprete (Musashi)
como respaldo para el ~3-5% de código que todavía no se descubrió
estáticamente. Resultado actual: **~95-97% nativo**, verificado bloque
por bloque contra el intérprete (0 instrucciones mal traducidas).

## Motor

Copia recortada del motor "Streets-of-Rage-Collection-Recomp" (ver
`../LICENSE`, MIT), genérico para "cualquier cartucho" vía
`tools/new-game.mjs`/`kit.mjs`. Se le sacó la capa específica de Streets
of Rage que Comix Zone no usa (su menú tabulado, vestuarios de
peleadores, selección de edición, pantalla de lanzador) — queda lo que
permite decomp/recomp genéricamente, más un **menú de pausa propio**
(Esc o el botón Select/"-" del control): save states, rebobinado/avance
rápido, y una página de **Cheats** (Infinite health, Infinite lives —sin
dirección confirmada todavía, no hace nada—, 1-hit kill enemies,
Multi-jump, Jump to level).

## Control de 6 botones

`games/comixzone/game.json` activa `"sixButton"`. El 8BitDo M30 2.4G
(pad con licencia Sega) funciona con los 6 botones completos conectado
por su dongle 2.4GHz propio; cualquier otro control (Xbox, PlayStation,
o el mismo M30 por cable USB) cae automáticamente a un esquema de 3
botones sin X/Y/Z, porque el protocolo real de 6 botones no resultó
confiable para pads genéricos — ver `runtime/src/io.c` y
`runtime/src/pads.c` para el detalle.

## Estado y bugs conocidos

Ver `../decomp/NOTES.md` para el diagnóstico detallado del bug de sonido
(ya resuelto, con desensamblado real del driver).

## Descompilación / análisis estático

`../decomp/` tiene un proyecto separado (con `sega2asm`) para leer el ROM
directamente — útil para seguir cerrando el 3-5% que falta o investigar
bugs a nivel de código original, sin depender de instrumentar el emulador.
