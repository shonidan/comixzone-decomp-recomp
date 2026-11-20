# Comix Zone — recompilación nativa jugable

## Jugar

```bash
./build/ComixZone "../rom/ComixZone_U.bin"
```

El ejecutable es standalone: no necesita ningún archivo más al lado, solo
el ROM (que no se incluye acá, ver `../rom/`). `--headless N` corre N
frames sin ventana; `--press FRAME:BOTONES:LEN` para scripts de entrada;
`--help` (o leer `runtime/src/main.c` en el motor) para el resto de flags.

## Qué es esto

Un ejecutable nativo (ELF x86-64, ~6MB) compilado a partir del ROM de
Comix Zone, sin Wine ni emulador — el código del 68000 original está
recompilado a C y compilado para la máquina, con un intérprete (Musashi)
como respaldo para el ~3-5% de código que todavía no se descubrió
estáticamente. Resultado actual: **~95-97% nativo**, verificado bloque
por bloque contra el intérprete (0 instrucciones mal traducidas).

## Motor compartido

Construido con **Tools/Streets-of-Rage-Collection-Recomp** (el motor
"Beyond Oasis", originalmente hecho para la colección de Streets of Rage,
genérico para "cualquier cartucho" vía `tools/new-game.mjs`/`kit.mjs`).
Es un motor compartido con otros juegos (SoR 1/2/3) — el crédito completo
al proyecto original corresponde agregarlo cuando esto se publique/comparta.

## Cómo reconstruirlo

```bash
cd ../../Tools/Streets-of-Rage-Collection-Recomp
node tools/kit.mjs comixzone          # build + descubrimiento de código + verificación + reporte
cp build/comixzone/ComixZone ../../ComixZone/source/build/
```

El reporte completo (funciones encontradas, % nativo, capturas) queda en
`Tools/Streets-of-Rage-Collection-Recomp/build/comixzone/report/report.md`
después de correr `kit.mjs`.

## Estado y bugs conocidos

Ver `Tools/Streets-of-Rage-Collection-Recomp/COMIXZONE_STATUS.md` (estado
general, historial de la sesión) y `../decomp/NOTES.md` (diagnóstico
detallado del bug de sonido, con desensamblado real del driver).

## Descompilación / análisis estático

`../decomp/` tiene un proyecto separado (con `sega2asm`) para leer el ROM
directamente — útil para seguir cerrando el 3-5% que falta o investigar
bugs a nivel de código original, sin depender de instrumentar el emulador.
