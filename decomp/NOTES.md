# Comix Zone - notas de descompilación

## Resumen ejecutivo de la sesión del 2026-11-22 — frame 1661/1662: descartado que divergiera antes (para retomar rápido)

**Contexto**: continuación directa de la parte 2 del 21/11 (más abajo). Esa
parte dejó como "próximo paso concreto" comparar la secuencia COMPLETA de
interrupciones (nivel+PC) entre nativo e intérprete desde bien antes del
frame divergente, para no asumir que la primera diferencia real cae justo
en el frame 1661/1662 solo porque ahí es donde el hash de 64KB de RAM deja
de coincidir.

**Hecho, con `IRQ_DEBUG=1` en ambos modos** (mismo repro de siempre: seed
1235, `--widescreen 0`, hasta frame 1665): se filtraron todas las líneas
`[irq]` (una por cada interrupción realmente tomada) de ambas corridas y se
comparó línea por línea. **Resultado: 117074 líneas idénticas en orden,
nivel y PC exacto** — recién en el frame 1662 (convención de `g_dbg_frame`,
= frame 1661 del hash-log) aparece la primera diferencia: la interrupción
de nivel 6 (VBlank) se toma en `$002804` (nativo) vs `$002CD0` (intérprete).

**Esto descarta la precaución que había quedado pendiente**: no hay ninguna
divergencia más temprana "escondida" por el hash de 64KB — el registro
completo de cuándo y dónde se toma cada interrupción, desde el arranque,
es perfectamente idéntico hasta este punto exacto. El problema es
genuinamente local a lo que pasa durante el frame 1662, no algo acumulado
antes que recién se nota ahí.

**Mirando qué son esas direcciones** (`generated/comixzone/main/recomp_00.c`):
`$002804` es `jmp $2850(pc,d0.w)` (una tabla de salto) y varias de las
direcciones vecinas que aparecen en el mismo tramo (`$002AAE`, `$002AF4`,
`$002B76`, `$002BDA`) son todas `move.w $0(a2,d0.w),(a1)+` — el mismo
patrón repetido a intervalos regulares, consistente con un loop que recorre
una lista (de objetos/sprites) y salta a una rutina específica por cada
uno vía tabla de salto. **Hipótesis más consistente con la evidencia**:
nativo e intérprete procesan una cantidad ligeramente distinta de esa
lista dentro del frame 1662 — un desvío de timing que recién cruza un
umbral ahí (después de 1661 frames perfectos), no una instrucción mal
traducida puntual. No confirmado del todo; no se llegó a identificar el
bloque específico sin cobertura en ese tramo.

**Próximo paso concreto si se retoma**: identificar qué bloque(s) sin
cobertura estática caen en el tramo de la tabla de salto de `$002804`
(`SCHED_DEBUG`/`$0022xx-$0027xx` ya señalado en la parte 2) y ampliar el
autoplay dirigido para que `--verify` los pueda inspeccionar bloque por
bloque — hoy caen al intérprete en ambos modos, así que `--verify` no los
toca y no se puede confirmar si alguno está mal traducido.

**Pausado acá por decisión del usuario** ("cerremos ya") — no es un bug que
afecte el audio ni la jugabilidad visible hasta ahora, ver sesiones
anteriores para el detalle completo.

## Resumen ejecutivo de la sesión del 2026-11-21, parte 2 — localizada la escritura, descartada una hipótesis de causa (para retomar rápido)

**Contexto**: continuación directa de la parte 1 de hoy (más abajo). Esa parte
dejó pendiente encontrar qué instrucción escribe RAM `$FF00C2/C3/C7/E3/E7` en
el frame divergente (1661/1662 según la convención de conteo — ver nota de
abajo) y por qué difiere entre nativo e interpretado.

**Herramientas nuevas, persistentes (cero costo si no están activadas), para
la próxima sesión**:
- `RAM_DEBUG=offset,offset,...` (hex, relativo a `$FF0000`): imprime frame, PC
  (del bloque nativo que escribe, o exacto si es el intérprete), origen
  (`native`/`interp`) y byte viejo/nuevo cada vez que cambia uno de los offsets
  dados. Hook en `wr8`/`wr16` (`runtime/src/recomp_rt.h`, camino nativo) y
  `bus_write8`/`bus_write16` (`runtime/src/bus.c`, camino intérprete). Declarado
  en `genesis.h`, implementado en `machine.c`.
- `IRQ_DEBUG=1`: imprime frame/nivel/PC cada vez que se toma una interrupción
  (`gen_int_ack`, `machine.c`) y, en el loop de fallback de `run()`, frame/PC/
  si la instrucción estaba cubierta (`bail`) o no (`uncovered`) cada vez que se
  cae al intérprete.

**Conteo de frames, aclaración**: el hash-log de `--hash-log` numera desde 0
(primera línea = frame 0); `g_dbg_frame` (usado por estas herramientas)
incrementa ANTES de procesar, así que su valor coincide con "número de
`machine_run_frame` ya completados", es decir, 1 más que el índice del
hash-log para el mismo frame real. La "divergencia en el frame 1661" del
hash-log y el "frame=1662" que imprimen estas herramientas son el MISMO
frame — no hay dos problemas distintos ahí, fue una confusión de esta sesión
que llevó a descartar por error, un rato, la pista de las interrupciones.

**Localizada la instrucción que escribe los 5 bytes**: con `RAM_DEBUG`,
ambos modos escriben esos offsets desde la MISMA instrucción — el prólogo del
manejador de VBlank (`$2340`, `movem.l d0-d7/a0-a6,-(a7)`, dentro de
`b_00233C` en `generated/comixzone/main/recomp_00.c`). La instrucción está
bien traducida (ya la leí completa, coincide 1:1 con el ASM). Lo que difiere
no es la instrucción sino los VALORES de los registros que guarda — porque la
interrupción de VBlank se toma en un PC distinto en cada modo (confirmado con
`IRQ_DEBUG`: nativo `$002804`, intérprete `$002CD0`, separados por ~1200
bytes). Esto es downstream, no la causa: el flujo de control ya había
divergido genuinamente ANTES de esta interrupción.

**Hipótesis probada y DESCARTADA**: que la causa fuera la cadencia de chequeo
de interrupciones de Musashi (`m68k_execute()` sólo revisa interrupciones
pendientes una vez por llamada — comentario original `/* ASG: removed
per-instruction interrupt checks */` en `third_party/musashi/m68kcpu.c`), y
que el loop de fallback de `run()` llamando `m68k_execute(1)` instrucción por
instrucción (en vez de una sola llamada grande, como hace el modo 100%
interpretado) revisara de más y tomara la interrupción antes de tiempo.
Implementé el fix (agrupar todo el tramo no cubierto en una sola llamada a
`m68k_execute()`, dejando sin tocar el caso de `BAIL` de una sola
instrucción), confirmé con las líneas `[sched]` de `IRQ_DEBUG` que el
fallback SÍ agrupa en una sola llamada ahora (tramos de cientos de ciclos, no
de a 1) — y la
interrupción se sigue tomando exactamente en los mismos `$002804`/`$002CD0`
de antes. **Revertido** (no se justifica mantener un cambio de scheduler
compartido que no soluciona nada). Si se retoma esto, no reabrir esta vía sin
un dato nuevo.

**Próximo paso concreto si se retoma**: el control de flujo ya difiere ANTES
del frame 1661/1662 — buscar hacia atrás, no en la interrupción en sí. Usar
`IRQ_DEBUG=1` para comparar la secuencia COMPLETA de interrupciones tomadas
(nivel+PC) entre nativo e interpretado desde bien antes (ej. frame 1600) y
encontrar la PRIMERA que no coincide exactamente (no asumir que es la del
frame 1661 solo porque ahí es donde el hash de RAM deja de coincidir — un
registro/dato puede divergir un rato antes sin que el hash completo de 64KB
lo note, si cae en una zona que se sobreescribe antes del siguiente muestreo
por frame). Candidatos para revisar en ese tramo (vistos en el trace de
`SCHED_DEBUG`, código no cubierto cerca de la interrupción): bloques en
`$0022xx-$0027xx` y `$1Cxxxx` — puede valer la pena cerrar cobertura ahí
primero (jugar más/ampliar el autoplay) para que `--verify` pueda
inspeccionarlos bloque por bloque, ya que hoy caen al intérprete en ambos
modos y por eso `--verify` no los toca.

**No se comiteó** la cobertura de esta sesión ni el build — sólo quedan para
comitear las herramientas de debug (`RAM_DEBUG`, `IRQ_DEBUG`/`SCHED_DEBUG`),
que son seguras e independientes del bug pendiente.

## Resumen ejecutivo de la sesión del 2026-11-21 — autoplay dirigido: un bug de verify arreglado, otro divergente sin cerrar

**Contexto**: sesión de autoplay dirigido (`source/tools/kit.mjs`, ampliado para
mezclar botones de 6 (X/Y/Z) y morir a propósito para forzar game-over/continue)
para subir cobertura más allá del ~63-97% previo. Encontró código nuevo (705→
más funciones/bloques), pero expuso una divergencia real nativo-vs-intérprete.

**Arreglado**: `--verify` (comparación bloque por bloque contra el intérprete,
`runtime/src/machine.c`) daba un falso positivo en bloques que tocan el puerto
de joystick ($1CB8B8 y vecinos, la detección de pad de 6 botones en
`runtime/src/io.c`). Causa: `th_pulses[2]`/`th_last[2]` (el conteo de pulsos de
TH para distinguir pad de 3 vs 6 botones) son `static` en `io.c`, **fuera** del
`Snapshot` que `--verify` restaura entre la corrida nativa y la del intérprete
— el intérprete heredaba el conteo ya avanzado por la corrida nativa y leía un
nibble distinto del mismo puerto. Arreglado agregando `io_pad_detect_save`/
`io_pad_detect_restore` (declaradas en `genesis.h`) y sumando `PadDetectState`
al `Snapshot` (`snap_take`/`snap_restore`). Confirmado: antes del fix,
`--verify` reportaba `MISMATCH block $1CB8B8: d reg native=00007F30
interpreter=00007F33`; después, 0 mismatches en ~7.27M bloques verificados
(1700 frames). Esto no afecta el juego real (no hay restore-and-replay fuera
de `--verify`), solo volvía no confiable la propia herramienta de verificación
para bloques de lectura de pad.

**Pendiente, NO arreglado**: una corrida nativa completa sigue divergiendo de
una corrida 100% interpretada (`--interpret`) a partir del frame exacto 1661
(mismo PC final `$1CDB6C` en ambas, mismo VRAM, pero RAM distinta) — frames
489-1660 son bit-idénticos. `--verify` no lo detecta (0 mismatches en ese
rango), lo que descarta que sea un bloque individual mal traducido en
aislamiento. Diffeando el dump completo de RAM (`--dump`) en el frame 1661:

- 5 bytes sueltos cambian de un valor estable (igual en ambas corridas por
  muchos frames) a valores completamente distintos: offsets RAM `$C2 $C3 $C7
  $E3 $E7` → nativo `03 98 46 44 CE`, intérprete `F0 00 B4 3B AA`.
- Además un bloque de ~13 bytes en `$76AC-$76CD` (paso de a 4, como una tabla
  de entradas) queda poblado en nativo pero en cero en el intérprete — probable
  efecto en cascada de lo anterior dentro del mismo frame, no la causa.
- `sub_1CB890` (el mismo lector de puerto de joystick de más arriba) se llama
  desde `$1E4AE0`/`$1E4B0E`/`$1E5836`, pero sus destinos conocidos
  (`$FFFFC18E`/`$FFFFC18A`) no coinciden con los offsets que cambian — así que
  puede ser una rutina DISTINTA la que escribe ahí (sin identificar todavía),
  o una lectura de timing-sensible (HV counter de VDP u otro registro de
  hardware usado como semilla) que diverge por un corrimiento acumulado de
  ciclos entre modo nativo e interpretado, no detectable bloque por bloque
  porque `--verify` resincroniza el estado antes de cada bloque.

**Próximo paso concreto si se retoma**: identificar qué instrucción escribe
RAM `$FF00C2`/`$FF00C3`/`$FF00C7`/`$FF00E3`/`$FF00E7` (probablemente
direccionamiento `(An)` con offset negativo, no literal, así que buscar en
`game.asm` por el patrón de offset en vez del address absoluto) y revisar si
lee un registro de hardware (puerto de joystick, VDP HV counter, YM2612
status) en el camino. Reproducir con: `./ComixZone <rom> --headless 1662
--press "<script de kit.mjs, seed 1235, frames 6000>" --widescreen 0 --dump
<file>` contra la misma corrida con `--interpret`, diffear los dumps.

**No se comiteó** la cobertura de esta sesión (`games/comixzone/coverage.txt`)
ni el build — sólo el fix de `--verify` (`runtime/src/io.c`,
`runtime/src/machine.c`, `runtime/src/genesis.h`), que es seguro e
independiente del bug pendiente.



**El bug está arreglado.** Parche de 2×2 bytes en el ROM (no en el motor),
confirmado con la misma herramienta de instrumentación que encontró el
bug originalmente.

**El parche**: el decodificador de nibbles/ADPCM en `$03E5`-`$0474` solo
tenía una salida real (`ret`), condicionada a que el Timer A del YM2612
lea "no desbordó todavía" — algo que, confirmado en la parte 7 con tres
métodos distintos (incluido un aislamiento de `ymfm` fuera del motor), es
aritméticamente imposible bajo el período que el propio ROM configura
para esta rutina. Se cambiaron dos saltos condicionales por NOPs, para
que la rutina se comporte como el camino simple (sin comprimir) que ya
funciona bien: procesa un nibble y vuelve, en vez de loopear esperando
al timer.

```
local $0430: 20 03  (jr nz,$0435)  ->  00 00  (nop nop)   -- ROM $003C12
local $0470: 20 c3  (jr nz,$0435)  ->  00 00  (nop nop)   -- ROM $003C52
```

Aplicado a `ComixZone/rom/ComixZone_U.bin` (el original queda intacto en
`ComixZone_U.bin.orig-backup`, SHA-1 `e8747eefdf61172be9da8787ba5be447ec73180f`).
Checksum del header del ROM recalculado y corregido (`$18E`-`$18F`:
`384e`→`f788`) — importante para hardware real/flashcarts, que si no lo
verán como ROM inválido. SHA-1 del ROM parcheado:
`555bbe20acc76edd016bd5b84d541e915361949a`.

**El motor exige que el ROM sea uno de los conocidos (verifica SHA-1,
`runtime/src/bus.c`)** — el hash de cada edición vive en
`generated/comixzone/main/edition.c` (campo `.sha1` de `game_edition_main`,
generado por `tools/recomp.mjs`, normalmente no se edita a mano). Se
actualizó ese hash al del ROM parcheado para que el motor lo acepte. Si
alguna vez se regenera ese archivo desde cero (`node tools/kit.mjs
comixzone`), hay que recordar reaplicar este cambio (o re-generar a partir
del ROM YA parcheado, no del original).

**Confirmado con `GEMS_DEBUG=1 --headless 7000`** (bien más allá del
frame 3537 donde SIEMPRE se colgaba antes): la detección automática de
cuelgue NO se dispara más (solo los dos falsos positivos de arranque en
frames 2 y 8, ya conocidos). Y, puntualmente, la MISMA llamada exacta que
antes colgaba para siempre (`frame=3534, de=0118 hl=2618 bc=0403`, el
mismo `mem03bd=18,26,00` "frío" de siempre) ahora **retorna** por el
punto de salida parcheado (`RET pc=0434 -> target=069c`), y el Z80 sigue
con actividad normal de CALL/RET miles de líneas después, hasta el final
de la corrida.

**No verificado todavía**: cómo suena exactamente ESE voice/sample
específico en adelante (el parche lo corta después de procesar un nibble
en vez de la cantidad "completa" que el diseño original — probablemente
buggeado — hubiera intentado procesar). Dado que es, aparentemente, una
muestra que NUNCA pudo funcionar como estaba (colgaba el juego entero
100% de las veces que se la invocaba), cualquier resultado audible es
mejor que silencio total para siempre. **Pendiente**: escuchar la
partida real (no headless) para confirmar que suena bien y no hay otro
efecto secundario audible.

**Solo se tocó `ComixZone_U.bin`** (la edición de referencia). Las
ediciones E/J (`ComixZone_E.bin`, `ComixZone_J.bin`) no se tocaron — si
comparten el mismo código Z80 en la misma dirección (muy probable, el
driver de sonido rara vez difiere por región), necesitarían el mismo
parche en sus propios offsets de ROM si alguna vez se quieren usar.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 7 — conclusión: NO es arreglable con timing (para retomar rápido)

**Esto cierra, con la evidencia más directa posible sin hardware real, la
vía de "arreglarlo con un fix de timing en el motor".** No seguir
insistiendo con el Timer A sin un dato nuevo que lo reabra.

**Se probó un TERCER fix, inspirado en el código real de BlastEm** (el
emulador más citado por su precisión ciclo-exacta): `genesis.c` de
BlastEm llama `sync_sound()` (que resincroniza el chip FM al ciclo EXACTO
del Z80) **antes de cada lectura o escritura al puerto FM**
(`z80_read_ym`/`z80_write_ym`), no una vez por línea como hacíamos
nosotros. Se implementó exactamente ese patrón (`fm_sync()` en
`z80bus.c`, llamado en cada acceso al puerto YM antes de `ym_read()`/
`ym_write()`). **Probado contra el mismo repro: CERO efecto, mismo frame
exacto (3537). Revertido.**

**Para asegurarse de que no era un bug nuestro de integración** (no solo
"no alcanza con tickear más seguido"), se aisló `ymfm` en un programa de
prueba standalone (fuera del motor, sin nuestro código alrededor) que
reproduce exactamente la secuencia real: configura el Timer A con el
mismo período (`$24`=`$FE`, `$25`=`$01`→periodo real ≈ 1008 "clocks"),
tickea, y escribe `$15` cada vez que ve el bit de overflow en 1. **El
mecanismo de reset SÍ funciona correctamente en aislamiento** — limpia el
bit, cuenta ~6-7 ticks, vence de nuevo, se repite.

**Y se confirmó también dentro del motor real** (instrumentando el
status ANTES y DESPUÉS de cada escritura a `$27`): el write de `$15`
**sí limpia el bit inmediatamente** cuando se lo mide justo ahí
(`frame=70: BEFORE=81 → AFTER=80`, confirmado). El problema NUNCA fue que
el reset fallara. Es que **una vuelta completa del loop (al volver a
chequear el status) tarda más tiempo real que el período configurado**
— así que para cuando se vuelve a leer, el timer corto YA venció de
nuevo, de forma genuina y correcta, no por un artefacto de nuestra
emulación. Esto es aritmética real (período del ROM vs. costo del loop
del ROM), no algo que ningún ajuste de "cuándo" o "cada cuánto" tickeamos
el timer pueda cambiar.

**Dato de contexto que refuerza esto, no lo contradice**: se clonó
`https://github.com/vladikcomper/MegaPCM` (sugerencia del usuario,
después de un dato pegado sin verificar sobre un supuesto parche para
Comix Zone — **no until nada que corrobore ese parche específico**,
tratarlo como no confirmado). Lo valioso de MegaPCM no es que sirva para
parchear Comix Zone (no hay integración con GEMS, y solo reemplaza DAC,
no el secuenciador completo) — es que su propio código **evita a
propósito usar el Timer A del YM2612 para el pacing**, usando en cambio
un loop de espera contado en ciclos de CPU puros + sincronización a
VBlank, **con auto-calibración al arrancar** (mide la velocidad real de
lectura ROM/RAM, porque distintos emuladores y revisiones de hardware
real tienen timings de bus distintos). Un autor moderno, deliberadamente,
evitó depender del Timer A por esta exacta razón de fragilidad —
consistente con el reporte de foro (Sega-16) de que Comix Zone se cuelga
en Genesis Model 1 pero no en Model 2/Nomad.

**Conclusión**: el bug es, con alta confianza, un comportamiento genuino
del ROM de 1995 (un período de Timer A demasiado corto para el costo de
su propio loop de decodificación de nibbles), no algo introducido por
nuestra recompilación ni arreglable ajustando el timing del motor. Las
únicas vías que quedan son: (a) aceptarlo como bug auténtico de época y
no tocarlo, o (b) editar el ROM (código del decodificador o datos de esa
muestra específica) — lo segundo es exactamente lo que el usuario pidió
no hacer por ahora.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 6 — fuente real de un juego hermano (Batman Returns, Sega CD)

El usuario pidió clonar `https://github.com/RetailGameSourceCode/BatmanReturns`
(código fuente real filtrado de Batman Returns, Sega CD — no es GEMS en
el sentido estricto del SDK de Recreational Brainware, pero comparte
claramente la misma familia de driver de sonido). Clonado en
`~/Documentos/BatmanReturns/` (fuera de este repo, no vendorizado acá).

**Confirma estructuralmente lo que ya habíamos reconstruido a mano** —
`Source/BRIANS/CDAD_Z.S`, rutina `main` (línea ~504-509):
```
main        exx             ; ints are disabled upon entry here
                            ; initialize alternate regs for GDACME calls
            ld  B,15H       ; timer reset command (also hold CH3 mode bits)
            ld  D,1FH       ; read pointer from GDACFIFO - msb always = 1FH
            ld  HL,4000H    ; points to base of FM chip
            exx
            ei
```
Esto se ejecuta **una sola vez, al boot**, y confirma exactamente:
- `B'=$15` es una constante GLOBAL fija de por vida (el "timer reset
  command", igual al `$15` que vemos escribirse al puerto de datos en
  cada iteración del loop de Comix Zone) — **no es algo que cada nota
  recalcule**.
- `HL'=$4000` también es fijo de por vida (base del chip FM).
- **`D'=$1F` también es fijo de por vida** — el puntero real al
  "GDACFIFO" siempre vive en la página `$1Fxx`; **solo el byte bajo
  (`E'`) cambia por nota**.

**Esto corrige un error nuestro de la parte 3/4**: el `de=0118` que
veníamos señalando como "el puntero de muestra roto" es el **banco
principal** de registros (capturado antes del `exx` de entrada a
`$03B0`) — **irrelevante para la rutina**, que tras el `exx` opera sobre
el banco SOMBRA, cuyo `D'` debería seguir siendo `$1F` fijo (heredado de
este `main:` de boot) salvo que algo lo pise explícitamente. No
confirmamos directamente el valor real de `E'`/`D'` sombra en el
momento del cuelgue (no lo instrumentamos) — **posible próximo paso si
se retoma**: loguear el banco sombra (no el principal) en la entrada a
`$03B0`, para confirmar si el puntero real SÍ cae en `$1Fxx` (lo
esperable) o no.

**Dato adicional de este archivo**: el soporte de voces "digital" (DAC)
en este build de Batman Returns está explícitamente sin terminar —
`DEALLOC: "for now - for digital do nuthing"` — y no hay ningún spin
sobre el Timer A en todo el archivo (solo spins de bit de busy, `bit
7,(HL)`, para escrituras FM normales). O sea: **este juego hermano ni
siquiera llega a implementar el streaming de muestras comprimidas por
nibbles vía Timer A que sí tiene Comix Zone** — no hay una
implementación "de referencia que funciona" para comparar línea a línea.
Refuerza (no prueba del todo) que el mecanismo de Comix Zone es una
extensión más avanzada y específica, no algo genérico de toda la familia
GEMS/Brainware.

**Ningún comentario de bug/advertencia conocido** en todo `CDAD_Z.S`
sobre el Timer A, el DAC, o cuelgues.

**Actualización — se instrumentó el banco sombra y se confirma: el
puntero NO está roto.** Agregado un snapshot de `de'`/`hl'`/`bc'` (no
solo el banco principal) en cada entrada a `$03B0`. Resultado en la
llamada que se cuelga: `de'=1f00 hl'=4000 bc'=1500` — **exactamente el
rango esperado** (`$1Fxx`, igual que las ~200 llamadas anteriores
exitosas de este mismo caller, que van de `$1f00` a `$1fff`). El
`de=0118` de las partes 3/4 queda confirmado como un error nuestro (era
el banco principal, irrelevante) — no hay ningún puntero corrupto ni
dato roto acá.

**Esto termina de descartar la hipótesis de "dato/puntero malo"**: la
llamada que se cuelga usa un puntero de muestra tan válido como
cualquier otra. La única diferencia real y confirmada sigue siendo:
`mem03bd=18,26,00` (en vez de `00,00,00`) — es decir, **es la primera
vez en toda la partida que se toma el camino de nibbles/ADPCM en
absoluto** (las ~200 llamadas anteriores con `de'` en `$1Fxx` SIEMPRE
tomaron el camino simple de 1 byte sin comprimir). No tenemos ninguna
ejecución histórica exitosa del camino de nibbles para comparar — sigue
siendo la primera y única vez.

**Balance de la investigación en este punto**: se descartaron, con
evidencia directa, tres hipótesis distintas (pacing general del Timer A,
granularidad de muestreo, puntero/dato corrupto). Lo único que se
sostiene con evidencia repetida es la conclusión estructural de la parte
5: la rutina de nibbles tiene una sola salida real, condicionada
exclusivamente al Timer A, y ese chequeo nunca se cumple bajo ningún
esquema de muestreo que probamos — consistente con que el período
configurado en este ROM sea, en sí mismo, más corto que una vuelta del
loop. Arreglar esto en el motor exigiría un modelo de timing ciclo-exacto
de todo el bus (68K/Z80/VDP), no un ajuste puntual — un esfuerzo mucho
mayor a lo ya probado.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 5 — el fix de pacing del Timer A se probó y se descartó (para retomar rápido)

**Contexto**: el usuario preguntó explícitamente por tocar el motor (no el
código del juego), así que se desensambló a mano, byte a byte desde el ROM,
la rutina completa `$03E5`-`$0474` (el decodificador de muestras
comprimidas al que salta `DACMEPROC` la primera vez, ver parte 4) — algo
que la parte 4 había dejado pendiente.

**Hallazgo clave: es un decodificador ADPCM/delta** — lee nibbles de `(DE)`,
los pasa por una tabla de deltas en `$02xx` (indexada con `ADD A,C` /
`LD L,A`), y acumula el resultado en `$03E9` (arranca en `$80` = silencio,
centro de escala). Confirma independientemente lo que ya había dicho una
búsqueda web: "Comix Zone has ADPCM".

**Pero la rutina entera tiene UNA SOLA salida real** (`ret`), en dos
copias idénticas (`$0432`-`$0434` y `$0472`-`$0474`), y ambas están
condicionadas **exclusivamente** a que el bit de overflow del Timer A
lea 0 (`bit 0,(HL)` sobre `$4000` después de seleccionar el registro
`$27`). Hay un bloque con `DEC C` / `JR NZ` (`$03EF`-`$03FC`) que al
principio parecía una salida alternativa por contador — pero **no es
alcanzable desde el camino que realmente se ejecuta** (el `jr` en `$03ED`
lo salta siempre), y aunque se alcance desde otro lado, converge en el
mismo chequeo de Timer A de todos modos. **No hay ninguna salida que no
dependa del Timer A.**

**Se probó un fix real en el motor (no solo instrumentación) y se
descartó, con test directo contra el mismo repro:**

Hipótesis: el motor solo actualiza los timers del FM una vez por línea de
video (`sound_advance()`, ~488 ciclos de 68K), así que un loop ajustado del
Z80 nunca lo ve transicionar de "no todavía" a "sí" dentro de esa ventana.
Fix probado: tickear los timers (`ym.tick_timers()`) en cada **instrucción**
del Z80 (`z80bus.c`), no solo una vez por línea — desacoplado de la
generación de audio real, que se mantuvo en su cadencia normal.

**Resultado: CERO efecto.** Mismo frame exacto del cuelgue (3537),
distribución del status casi idéntica (siguió leyendo "1" ~99.7% de las
veces). **Revertido** — no quedó aplicado en el motor.

**Por qué no funcionó, y qué significa**: no era un problema de
granularidad de muestreo. El período configurado de Timer A en este ROM
es, en sí mismo, más corto que una vuelta completa del loop (unas 20-26
instrucciones del Z80) — así que no importa con qué precisión se lo
tickee, YA desbordó de nuevo para cuando el loop vuelve a chequear. Esto
es, en principio, el comportamiento correcto para ESE período específico,
no un bug de nuestra emulación — lo cual implica algo incómodo: **esta
rutina, tal como está escrita, se colgaría exactamente igual en hardware
real**, salvo que la relación real entre ciclos de Z80/FM y tiempo de
pared en el hardware original sea sutilmente distinta a la nuestra (por
ejemplo, wait-states de arbitraje de bus entre 68K/Z80/VDP que nosotros no
modelamos ciclo a ciclo). Confirmar o descartar ESO requeriría un modelo
de timing ciclo-exacto de todo el bus compartido — un esfuerzo mucho
mayor y más riesgoso que los dos intentos ya probados, no algo para
encarar sin que el usuario decida explícitamente que vale la pena.

**Estado de la investigación en este punto**: se probaron y descartaron,
con evidencia directa de repro, las dos hipótesis de motor más plausibles
(parte 4: pacing general; parte 5: granularidad de muestreo). Lo único
que queda sin probar del lado motor es un timing ciclo-exacto completo
(gran esfuerzo). Del lado dato/código del juego, sigue sin confirmarse si
el bug es de los BYTES de esta muestra específica o del CÓDIGO del
decodificador — pero el usuario pidió explícitamente no tocar eso por
ahora.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 4 — CAUSA RAÍZ encontrada (para retomar rápido)

**Esta es, hasta ahora, la mejor explicación con evidencia real de por qué
se cuelga, y por primera vez apunta a un FIX DE MOTOR concreto, no a
editar bytes del ROM.**

Siguiendo la pista de la parte 3 (el caller `$0699`, llamada anómala con
`hl=2618 bc=0403`), se desensambló a mano (byte a byte desde el ROM, no
confiando en `sega2asm` — ver nota sobre sus límites más abajo) la cadena
completa: `$0699` → `$11E2` (muy probablemente `NOTEON`) → un bloque en
`$13D3`-`$140B` que se ejecuta **una sola vez, al procesar el note-on**.
Ese bloque:

1. Escribe `$D9`/`$08` (`EXX`/`EX AF,AF'`) en `$03B0`/`$03B1` — arma el
   trampolín auto-modificable de `$03B0` (igual que `DACMEALT` en el
   fuente real de GEMS).
2. **Lee un byte de estado en `$140C`, lo enmascara con `$0F`, y lo
   compara contra 10**: si el resultado enmascarado es `< 10`, escribe
   `$C9` (`ret`) en `$03C8`; si es `>= 10`, escribe `$00` (`nop`) ahí.
   `$03C8` guarda, con eso, si el trampolín de `$03B0` **se resetea a su
   estado original después de este note-on, o se queda "armado" para
   seguir sonando indefinidamente** (el código en `$03C9`-`$03D7` que
   sigue a ese `nop`/`ret` es exactamente la rutina que restaura
   `$03B0`/`$03B1` a sus bytes originales desde `$03AC`/`$03AD`).
3. Carga `HL` desde `$03AE` **solo si el bit 7 de ese mismo byte de
   `$140C` está en 1** (si no, `HL=$0000`), y lo guarda en `$03BD`.

**Esto es, con información suficiente, el flag de "sample en loop"**: un
one-shot normal deja el trampolín listo para resetearse apenas termina;
un sample/nota en modo loop (ambiente, música de fondo vía DAC) lo deja
**armado a propósito**, confiando en que el propio Timer A vaya pausando
la reproducción en tiempo real para que el driver pueda volver al loop
principal entre una muestra y la siguiente — y que algún evento FUTURO
(otro note-on/note-off de ese canal) lo corte cuando corresponda.

**Por qué esto cuelga el juego, reconciliando TODO lo medido hasta
ahora**: ya se confirmó (parte 2) que el overflow del Timer A se lee
"siempre en 1" desde temprano en la partida — nuestro `ymfm`/wrapper NO
pacea el Timer A en tiempo real como el hardware real. Para una nota
one-shot esto es inofensivo (el loop igual termina solo, por su propio
contador de longitud — el mismo `mem03e9=$80` que vimos siempre igual en
cada entrada). **Para una nota en modo loop, en cambio, es fatal**: el
diseño original CONFÍA en que el Timer A real vaya cediendo el control
periódicamente; sin esa pausa real, el Z80 procesa el buffer circular de
muestras (chico, de unos pocos bytes) a velocidad de CPU sin parar,
nunca vuelve al bucle principal, y por lo tanto nunca puede recibir el
comando que lo pararía. Esto coincide exactamente con: por qué pasa
siempre al mismo tiempo de juego (lo dispara la música/ambiente, no el
jugador), por qué es la PRIMERA vez que pasa algo así en la partida (es
la primera vez que suena una nota en modo loop con este patrón, en
~3534 frames de juego previos todas fueron one-shot), y por qué el
cuelgue es total e instantáneo en vez de degradarse gradualmente.

**Esto implica que el fix más probable está en el MOTOR (`sound.cpp`:
`tick_timers()`/`timer_left[]`/`ymfm_set_timer()`), no en editar bytes
del ROM** — contrario a lo que se pensó en la parte 2 original
("CAUSA RAÍZ MÁS PROBABLE... bug documentado de GEMS", sección vieja más
abajo, ya marcada como superada). **No intentado todavía**: un fix real
de pacing del Timer A es un cambio de comportamiento que afecta TODO el
audio del juego, no solo este caso — amerita probarlo con cuidado contra
toda la partida, no solo este repro, antes de darlo por bueno. Ya se
había probado y descartado un fix relacionado (`cycle_debt` en busreq,
parte 2) pero apuntaba a otra hipótesis (pérdida de ciclos del Z80, no
pacing del Timer A en sí) — no es el mismo fix.

**Nota sobre `sega2asm`**: tiene un bug sistemático con prefijos
`$DD`/`$FD` (instrucciones de IX/IY) — los emite como `defb` sueltos y
desincroniza todo lo que sigue. Pasó en varios puntos de esta sesión
(`$0066`+, `$0689`, `$06EB`, etc.). La única forma confiable de leer estas
zonas fue volcar los bytes crudos del ROM con un script de Python y
decodificar a mano. Si se retoma el desensamblado estático, vale la pena
parchear `sega2asm` para manejar IX/IY antes de seguir confiando en su
salida en bloques con muchos `defb`.

**Próximo paso concreto**: revisar `tick_timers()`/`ymfm_set_timer()` en
`sound.cpp` para entender por qué el overflow del Timer A casi nunca se
lee en 0, e intentar un fix real de pacing (no solo para este bug — mejora
la fidelidad general del FM). Probar el fix contra este mismo repro
(`GEMS_DEBUG=1 --headless 3545`, cero input) Y contra una sesión larga
jugada a mano, para confirmar que no regresiona el audio normal.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 3 (para retomar rápido)

**Corrección importante a la parte 2** (debajo): `$03B0` **NO es
exclusivamente "DACME"** — es una subrutina genérica compartida, con
**más de 40 sitios de llamada distintos** en todo el driver (confirmado
contando callers únicos en la traza de CALL/RET). Es casi seguro el
trampolín genérico de intercambio de bancos de registros (`exx`/`ex
af,af'`, el mismo patrón auto-modificable de `DACME` en el fuente real),
reutilizado por muchas rutinas de voz/canal, no solo por el reproductor
de PCM. La lectura "está en DACME, el reproductor de PCM" de la parte 2
es **demasiado específica** — lo correcto es "está en la subrutina
compartida en `$03B0`, invocada esta vez con el mismo propósito que el
reproductor de PCM de la referencia (sí escribe al puerto DAC), pero el
bug en sí podría estar del lado del LLAMADOR, no de esta subrutina".

**Hallazgo nuevo, más preciso, con traza de registros en cada entrada**
(agregado un snapshot de `DE`/`HL`/`BC`/`A` en cada llamada a `$03B0` —
ver "Herramientas nuevas" abajo): la llamada que se cuelga viene del
caller **`$0699`**, el mismo que ya había llamado a `$03B0` **171 veces
antes sin problema** en esta misma corrida, siempre con la forma
`de=0118 hl=0000 bc=0003 a=XX` (con `XX` variando entre `$07`/`$25`/
`$45`/`$46`/`$65` — probablemente un índice de voz/canal o un byte de
estado). **La llamada número 172, la que cuelga, es la ÚNICA de toda la
corrida con `hl=2618 bc=0403 a=10`** — `hl` y `bc` nunca antes vistos
distintos de `0000`/`0003` para este caller. Es evidencia fuerte de que
esto es un caso límite de los DATOS (una combinación de voz/evento que
nunca había ocurrido en los ~59 segundos previos) disparando una rama no
manejada correctamente dentro de la subrutina compartida — no algo
relacionado con el Timer A, el FIFO de comandos, ni el GOTO/LABEL de
duración cero (todos ya descartados).

**Próximo paso concreto**: desensamblar con cuidado qué hace el cuerpo de
`$03B0` con `HL`/`BC` específicamente (no solo con `DE`, que ya se mapeó
en la parte 2) — buscar dónde los usa para decidir cuántas muestras
procesar o cuándo cortar el loop, y por qué `bc=0403`/`hl=2618` en vez de
`bc=0003`/`hl=0000` hace que esa condición de corte nunca se cumpla.
También identificar qué es el caller `$0699` en sí (qué evento de
juego/música dispara esta llamada número 172 con esos registros
distintos) — buscar sus propios callers con la misma técnica de traza.

## Resumen ejecutivo de la sesión del 2026-11-20, parte 2 (para retomar rápido)

**Continuación de la parte 1 de hoy** (más abajo, "El bug del 'sonido se
rompe'" y siguientes secciones). Esta parte agrega instrumentación nueva
y **identifica con precisión de instrucción exacta** en qué rutina se
cuelga el Z80 — algo que la parte 1 había dejado como "próximo paso".

**Hallazgo nuevo y confirmado con traza instrucción-por-instrucción**:
el Z80 se queda para siempre dentro de **`DACME`** (local `$03B0` en este
build, confirmado byte-a-byte contra el código fuente REAL de GEMS en
`Tools/GEMSPlay/asm-sources/Z80_2-8_cleaned.ASM` líneas 460-526, incluido
su trampolín auto-modificable característico "jr DACMEALT ... cambia a
EXX/EX AF para habilitar"). `DACME` es la rutina que reproduce muestras
PCM por DAC, paceada por el Timer A del YM2612. **Nunca más ejecuta un
CALL ni un RET** desde que entra — motivo por el cual la traza de
profundidad de pila (que la parte 1 había dejado como "próximo paso") no
sirvió sola: hubo que agregar además una traza plana de PC instrucción a
instrucción (ver "Herramientas nuevas" abajo) para encontrarlo.

**Esto CONTRADICE la hipótesis anterior** ("causa raíz más probable:
bug de loop GOTO/LABEL de duración cero en los DATOS de la secuencia
musical de GEMS", sección más abajo) — esa hipótesis nunca se confirmó
con una traza real (la propia parte 1 lo admite: "no se llegó a
confirmar/descartar esto con un fix verificado"), y la traza real
muestra que el Z80 NO está en `seqcmdloop`/`seqgoto` (el intérprete de
secuencias) sino en `DACME` (el reproductor de PCM), un subsistema
completamente distinto. **No seguir investigando la hipótesis del GOTO
de duración cero** salvo que aparezca evidencia nueva que la reabra.

**El mecanismo exacto, confirmado con traza real** (ver
`$03B0`/`DACME` más abajo para el detalle completo):
- `DACME` normalmente: lee el status del YM2612 (bit 0 = overflow del
  Timer A), si ya desbordó resetea el timer (escribe al registro `$27`),
  lee el siguiente byte de su FIFO de muestras PCM, lo escribe al puerto
  DAC (`$2A`), y en el código de referencia hace **un** `ret` por llamada.
- **El build de Comix Zone en cambio hace un LOOP interno** alrededor de
  ese ciclo (confirmado: el PC vuelve una y otra vez a `$0435` sin pasar
  nunca por ningún `ret`) — procesa muchas muestras por llamada, no una.
- **Dato sorpresa, NO es nuevo en el frame del cuelgue**: el bit de
  overflow del Timer A **ya se lee casi siempre como "siempre desbordado"
  desde el frame 100** (319 lecturas en `$00` contra >300.000 en total,
  prácticamente desde el arranque) — es decir, `DACME` lleva corriendo
  TODO el juego "a toda velocidad" sin la pausa de sincronización real
  que el hardware real impondría, y **eso no es lo que cuelga el juego**
  (el audio sonó bien ~59 segundos así). La causa del cuelgue tiene que
  estar en la condición de salida del loop interno de ESTE build (algo
  que decide "FIFO de PCM vacío, volvé" y que aparentemente falla una
  sola vez, para siempre, en vez de en la sincronización con el Timer A.

**Herramientas nuevas en `GEMS_DEBUG=1`** (persistentes, costo cero si no
está activado — ver sección dedicada más abajo para el detalle):
1. Traza de CALL/RET con profundidad (reconstruida de instrucciones, no
   del SP real) — útil para ver el patrón normal de llamadas.
2. **Detección automática de cuelgue**: si pasan 2 frames seguidos sin
   ningún CALL/RET, vuelca las siguientes 400 instrucciones PC-a-PC
   (acotado, porque el cuelgue real es infinito). No hace falta saber de
   antemano el número de frame — se dispara solo.
3. Traza incondicional de cada lectura/escritura al puerto del YM2612
   (no solo los registros que `sound.cpp` ya reconocía) — fue clave para
   ver que el loop SÍ escribe al puerto DAC, solo que a un registro que
   la traza vieja no miraba.

**Próximo paso concreto si se retoma**: con `GEMS_DEBUG=1 --headless
3545` (reproduce con CERO input, no hace falta script de botones) se
dispara el volcado automático en frame ~3537. Encontrar en el 68K qué
rutina rellena el FIFO de muestras PCM de `DACME` (separado del FIFO de
comandos de `$1B40`, este vive en `$1F00-$1FFF` según el código de
GEMS) y su condición "está vacío" — comparar el estado de esos punteros
en la llamada que se cuelga contra llamadas anteriores exitosas. Muy
probablemente conecta con el hallazgo ya confirmado en la parte 1 de que
algún objeto/timer del lado 68K (el de la bandera `$FFFFBFD6`) deja de
tickearse en este mismo momento — sería el candidato más directo para
"quién debería estar rellenando este FIFO y dejó de hacerlo".


Proyecto de descompilación estática dedicado a Comix Zone, separado del motor
de recompilación jugable (`../source/`, basado en
`Tools/Streets-of-Rage-Collection-Recomp`). Usa `Tools/sega2asm` (genérico,
soporta el formato de compresión propio de Comix Zone: `lzsti`).

ROM de referencia: `../rom/ComixZone_U.bin`, SHA-1
`e8747eefdf61172be9da8787ba5be447ec73180f` (verificado por tres vías
independientes: extracción de la versión XBLA, checksum interno del header
de Genesis, y comparación byte a byte contra un ZIP de fuente distinta).

## Estado: punto de partida, NO un split completo

Solo hay dos segmentos mapeados (`comixzone.yaml`):

- `header` ($000000-$000200): vectores de interrupción + header estándar.
- `sound_driver` ($0037E2-$0057E2): el driver de sonido del Z80 completo
  (8KB, 1:1 con el espacio de direcciones del Z80 $0000-$1FFF).

El resto del ROM (~2MB) no está mapeado todavía. Para un split completo
haría falta: encontrar dónde arranca el código 68K principal, identificar
todas las tablas de punteros/saltos, y marcar cada bloque de gráficos
comprimidos (formato `lzsti`, según `Tools/sega2asm/README.md`). Es un
proyecto de varias sesiones, al estilo `s1disasm` (Sonic 1) o el desensamblado
de X-Men 2 — no algo para resolver de una sentada.

## Cómo se encontró `$37E2` (dirección de origen del driver de sonido)

1. Se agregó temporalmente un volcado de `bus.zram` (8KB) al final del
   `--dump` del motor recompilado (revertido después, no queda en el
   código del motor).
2. Se comparó esa captura byte a byte contra el ROM completo con un script
   Python — coincide exacto a partir de `$37E2` (confirmado con dos
   fragmentos separados de 16-32 bytes cada uno, sin un solo byte distinto,
   descartando coincidencia).
3. El resto de la RAM del Z80 diverge gradualmente del ROM a medida que el
   driver corre (variables de trabajo que cambian), lo cual es esperable y
   no afecta la ubicación del código.

## El protocolo de comandos de sonido (68K → Z80)

Reconstruido leyendo el 68K ya recompilado (comentarios con la instrucción
original) y el Z80 desensamblado acá (`out/asm/z80/sound_driver.asm`).

**Direcciones de RAM del Z80** (vistas desde el 68K como `$A0xxxx`):
- `$0036`: índice de ESCRITURA (el 68K lo incrementa y confirma al final
  de cada depósito).
- `$0037`: índice de LECTURA (el Z80 lo incrementa cada vez que consume
  un byte).
- `$1B40`-`$1B7F`: buffer circular de comandos, **64 bytes**
  (`índice & $3F`).

**Lado 68K (depósito)** — rutina compartida en `$1CB53E`-`$1CB574`:
1. `$1CB53E`-`$1CB558`: sube BUSREQ y deshabilita interrupciones
   (`ori #$700,sr`), lee el índice de escritura actual de `$0036`.
2. El `rts` intermedio en `$1CB572` **no libera nada** — deja BUSREQ e
   interrupciones deshabilitadas a propósito; es la mitad de una región
   crítica que termina más adelante en `$1CB574`.
3. El llamador escribe 1 o 2 bytes en el buffer (ver estilos abajo),
   usando `$1CB592` (escribe un byte = el valor real, en `d0`) y/o
   `$1CB586` (escribe `$FF` primero, cae en `$1CB592` después).
4. `$1CB574`: baja BUSREQ, restaura SR (reactiva interrupciones), limpia
   la pila, `rts` final — recién acá termina la región crítica.

**Confirmado: NO hay ventana sin proteger.** Toda la secuencia (leer
índice + escribir 1-2 bytes + confirmar índice) es atómica de punta a
punta. Una hipótesis anterior de "condición de carrera" (sesión
2026-11-20) fue **descartada** tras releer el flujo con cuidado.

**Dos estilos de depósito**, 6 puntos de entrada encontrados
(`$1CB5A0`, `$1CB5B0`, `$1CB60C`, `$1CB622`, `$1CB632`, `$1CB640`):
- Directo (1 byte): valor dinámico tomado de un parámetro del llamador.
- Con marcador (2 bytes): escribe `$FF` y después un valor — en los casos
  vistos (`$1CB632`→`$0C`, `$1CB640`→`$0D`) el valor es **fijo**, no un
  parámetro. Probablemente "detené lo que estés haciendo, tocá este cue
  específico" (un reset + comando, no un marcador especial que el Z80
  interprete de forma distinta — el Z80 simplemente procesa `$FF` como un
  comando más, en secuencia).

**Lado Z80 (lectura)** — rutina en `$01CF`-`$01F4` (dirección Z80, =
ROM `$37E2+$01CF` = `$39B1`):
```
espera mientras ($0037) == ($0036)      ; nada nuevo
lee el byte en $1B40 + ($0037 & $3F)
incrementa $0037 en 1, lo confirma
devuelve el byte en 'a'
```
Avanza de a **1 byte por llamada**, sin importar si lo que depositó el 68K
fue de 1 o 2 bytes — eso es responsabilidad de quien llama a esta rutina
(13 call sites encontrados, uno de ellos usa el valor devuelto para indexar
una tabla de 32 bytes por entrada en `$1B80`, probablemente definiciones de
voz/efecto FM).

## Fixes reales aplicados al motor (no al ROM) — 2026-11-20

Estos dos SÍ son cambios de código aplicados y confirmados compilando,
en `Tools/Streets-of-Rage-Collection-Recomp/runtime/src/sound.cpp` (no
afectan el ROM ni el timing del 68K/Z80, solo la síntesis/mezcla de
audio):

1. **Bandera de "busy" del chip FM nunca implementada.** `ymfm` expone
   los hooks `ymfm_is_busy()`/`ymfm_set_busy_end()` (ver `ymfm.h`) para
   que la aplicación trackee cuánto dura el estado ocupado después de
   cada escritura de registro (documentado: 32 ciclos típico en
   `ymfm_opn.cpp`). Nuestra clase `Ym` no los sobreescribía — usaba el
   stub por defecto (`return false`, nunca ocupado). Confirmado en el
   código fuente real de GEMS (`WRITEFM` en `Z80_2-8_cleaned.ASM`) que
   el driver hace `bit 7,(HL); spin on busy bit` antes de **cada**
   escritura de registro FM, sin excepción. Sin esto, GEMS escribe
   registros más rápido de lo que el hardware real permite. Coincide con
   un bug ya documentado en MAME (issue #10511: "inexactitud de
   `fm_busy` causa problemas de audio en juegos de Genesis"). Fix:
   agregado `busy_left` trackeado igual que los timers existentes,
   decrementado en `tick_timers()`.
2. **Sin filtro de paso bajo en la salida final.** El hardware real
   filtra analógicamente la salida (~3 kHz), lo cual es parte de por qué
   el "ladder effect" del YM2612 (distorsión real del chip a bajo
   volumen, que mantuvimos — usamos `ymfm::ym2612`, no `ym3438`, para
   fidelidad histórica) suena "cálido" en una consola real en vez de
   ruido digital crudo. Agregado un filtro RBJ cookbook (biquad, Q=0.707,
   3 kHz) siempre activo (no es una opción de calidad, es hardware),
   aplicado en ambos caminos de `sound_end_frame()` antes del clip final.

**Pendiente de confirmar con el usuario si esto resolvió el "ruido"**
que reportó durante juego en vivo (distinto del bug de silencio total
documentado abajo, que es de los DATOS de la secuencia musical, no del
motor).

## El bug del "sonido se rompe" — diagnóstico final (2026-11-20)

**NO está relacionado con pegarle a un enemigo.** Es un bug de **tiempo
puro**: reproducido de forma determinística en el frame 3903 con un script
de entrada fijo (semilla 1235/777), e **idéntico en modo nativo e
intérprete puro** (`--interpret`) — descarta cualquier bug de traducción
del recompilador.

Evidencia (de `mailbox_trace.log`, sesión 2026-11-20): el índice de
escritura sube de a 2 cada ~150 frames (2.5s reales) de forma
perfectamente periódica — `37, 39, 41, ..., 63` — y dentro del MISMO frame
en que se depositan `$FF` + un valor, **da la vuelta** de 63 a 1 (65 mod
64 = 1) **justo en el frame 3902**, y el driver deja de reaccionar desde
ahí.

**Hipótesis más probable**: el puntero de lectura del Z80 ($0037) va más
lento que el de escritura (quizás porque procesar cada comando real le
toma al Z80 más que el intervalo de 2.5s entre depósitos de este cue en
particular, o por alguna otra razón aún no confirmada). Cuando el índice
de escritura **da la vuelta completa** y queda "detrás" del de lectura en
términos absolutos, la comparación de igualdad simple (`$0036 == $0037`)
ya no puede distinguir correctamente "nada nuevo" de "el productor le dio
toda la vuelta al consumidor" — un bug clásico de buffer circular que no
trackea una cuenta total además de los índices envueltos.

Como el timing es fijo (~65 segundos desde el arranque, no depende de
qué hace el jugador), esto **muy probablemente es un bug genuino del
juego original de 1995**, reproducible en hardware real bajo la misma
condición de tiempo — no algo introducido por la recompilación. La
coincidencia con "justo cuando le pego al primer enemigo" es porque
llegar al primer combate desde el arranque (logos, título, intro de
episodio, diálogo) tarda predeciblemente esos mismos ~60-70 segundos.

### Confirmado: es el driver GEMS (2026-11-20, vía investigación externa)

Comix Zone usa el driver de sonido **GEMS** ("Genesis Editor for Music and
Sound effects", encargado por Sega of America a Recreational Brainware) —
confirmado por DOS vías independientes:

1. **Hidden Palace** (`hiddenpalace.org/News/Sega_of_Japan_Sound_Documents_and_Source_Code`)
   nombra explícitamente a Comix Zone (junto con Sonic Spinball y Garfield:
   Caught in the Act) como uno de los juegos que usan GEMS.
2. **Coincidencia de bytes real**: el binario de referencia del driver GEMS
   (`Player/GEMS_Player.bin` del repo `realmonster/GEMS`, licencia LGPL)
   coincide **120 bytes exactos, sin un solo byte distinto**, contra el ROM
   de Comix Zone en `$1E3B2E` — estadísticamente imposible por azar.
3. La plantilla oficial del driver (`Player_template.asm`, mismo repo)
   documenta `$A01B80` como el **"ccb ptr"** (Channel Control Block
   pointer) — cae justo al lado de nuestro buffer `$1B40`-`$1B7F`
   (64 bytes antes), consistente con que sea la MISMA región de memoria
   del driver (el buffer de comandos justo antes del array de CCBs).

**Confirmación cruzada contra otro juego GEMS (Kid Chameleon)**: el repo
`sonicretro/kid-chameleon-disasm` (descompilación pública y comentada de
Kid Chameleon, que también usa GEMS) tiene el código de interfaz 68K→GEMS
disassemblado con nombres propios. Es **estructuralmente idéntico** al de
Comix Zone, confirmando que todo esto es código GEMS estándar (no algo
propio de Comix Zone):

- `sub_E143A` (Kid Chameleon) = `$1CB53E` (Comix Zone): mismo helper exacto
  (sube BUSREQ, `or #$700,sr`, `lea ($A00036).l,a0` — **la dirección
  `$0036` es IDÉNTICA en ambos juegos**, confirma que es fija/hardcodeada
  en el driver GEMS, no algo que cada juego reubica).
- `sub_E1482` = `$1CB586`: escribe literal `#$FF`, incrementa+envuelve el
  índice, **sin `rts`** — cae directo en la siguiente rutina, exactamente
  igual.
- `sub_E148E` = `$1CB592`: escribe el valor real (d0), incrementa+envuelve,
  confirma el índice, `rts`.
- `loc_E1470` = `$1CB574`: baja BUSREQ, restaura SR, limpia pila, `rts`.
- **`sub_E155A`, con `moveq #$C,d0` seguido de un solo `jsr sub_E1482`
  (que cae en `sub_E148E`) y `jmp loc_E1470`, es un calco exacto de
  nuestro `$1CB632`** (mismo ID `$0C`, misma estructura de 2 escrituras).
  `sub_E1568` (ID `$0D`) calca igual a `$1CB640`.

Única diferencia entre los dos juegos: la ubicación del buffer circular en
sí (`$A01A40` en Kid Chameleon vs `$A01B40` en Comix Zone — cada build del
driver lo coloca en un lugar distinto según cuántos otros datos van antes
en la RAM del Z80), pero el **mecanismo, los IDs de comando `$0C`/`$0D`,
y la dirección `$0036` del índice de escritura son 100% iguales**.

La documentación propia de Kid Chameleon (`doc/sound.txt`) no menciona
ningún bug de silencio/cuelgue ni detalla el buffer circular — son solo
links a herramientas externas. El repo dice tener "el driver de sonido
completo" en una carpeta `sound/`, pero esa carpeta **no existe** en el
árbol real del repo (probablemente removida por derechos de autor de
GEMS, igual que a nosotros nos faltó el `GEMS.asm` original).

**Límite de la investigación externa**: ni el repo `realmonster/GEMS`
(herramientas de investigación de GEMS, LGPLv3) ni ninguna otra fuente
pública documenta el mecanismo de cola/buffer circular específico que
encontramos — es un área que la propia comunidad de investigación
reconoce como no documentada ("cada variante del driver tuvo que
desensamblarse a mano", según Hidden Palace). El código fuente comentado
original de GEMS (de Recreational Brainware) no está archivado
públicamente en ningún lado que hayamos encontrado. **Este documento es,
hasta donde sabemos, el análisis más detallado disponible de este
mecanismo puntual.**

### Código fuente REAL de GEMS encontrado (2026-11-20)

El repo `ValleyBell/GEMSPlay` (clonado en `Tools/GEMSPlay/`) incluye
`asm-sources/Z80_2-8_cleaned.ASM` (y versiones 2.0/2.5) — el **código fuente
original del driver Z80 de GEMS**, no una reconstrucción. Confirma nuestra
ingeniería inversa exactamente:

```asm
****************************** Command FIFO (from 68000) *************************************
*  GETCBYTE - returns the next command byte in the fifo from the 68k. will wait
*    for one if the queue is empty when called.
GETCBYTE	push	BC
		push	HL
getcbytel	call	DACME
		call	FILLDACFIFO
		ld	A,(CMDWPTR)
		ld	B,A
		ld	A,(cmdrptr)		; compare read and write pointers
		cp	B
		jr	Z,getcbytel		; loop if equal
		ld	B,0
		ld	C,a
		ld	HL,cmdfifo
		call	DACME
		zadd	HL,BC
		inc	A
		zand	3FH			;  (mod 64)
		ld	(cmdrptr),A
		ld	A,(HL)
		pop	HL
		pop	BC
		ret
```

`CMDWPTR`/`cmdrptr` = nuestros `$0036`/`$0037`; `cmdfifo` = nuestro
`$1B40`. **El algoritmo no tiene NINGUNA protección contra desborde** —
confía por completo en que el Z80 real siempre llegue a tiempo. Esto no es
un bug nuestro ni del juego: es una limitación inherente y conocida del
diseño del driver, que en hardware real simplemente nunca se manifiesta
porque el timing es correcto.

**Pista nueva, más precisa, sobre el bucle principal** (mismo archivo,
línea ~1525):
```asm
		ld	A,(CMDWPTR)
		ld	B,A
		ld	A,(cmdrptr)
		cp	B
		jp	Z,loop			; nada esperando, seguir
		call	GETCBYTE		; main loop
		cp	0FFH			; ¿es inicio de comando?
		jp	NZ,loop			; NO -- se descarta en silencio, sin re-sincronizar
		call	GETCBYTE		; recién ahora el comando real
```
El parser **exige que la posición "par" del stream sea exactamente `$FF`**,
o descarta el byte sin ningún intento de resincronizarse. Si el buffer
circular da la vuelta de una forma que corre la fase par/impar entre
escritura y lectura, el parser queda desalineado **para siempre**: sigue
leyendo los comandos reales (`$0C`, etc.) justo donde espera un `$FF`, los
descarta todos, y nunca se recupera por sí solo — coincide exactamente con
el síntoma ("se traba y no vuelve"), y con que ocurra justo en el frame
donde el índice da la vuelta completa (63→1).

### Intento de fix probado y descartado (2026-11-20)

Inspirado en un bug real documentado en Gens4All (un juego GEMS -
Vectorman- que "a veces se colgaba"; el desarrollador lo atribuyó a que
"el Z80 perdía tiempo en los accesos del 68K a su RAM"), se probó cambiar
`z80bus.c`:
```c
if (bus.z80_busreq) { cycle_debt = 0; return; }   // original
if (bus.z80_busreq) { return; }                   // probado: preservar cycle_debt
```
**Resultado: NINGÚN cambio.** Se reprodujo el freeze en el mismo frame
exacto (3902-3903), con precisión de 0.25s en el análisis de audio. Esta
hipótesis específica queda **descartada** — revertido, no dejar este
cambio aplicado. La causa de que nuestro Z80 se atrase (si es que
realmente se atrasa — ver teoría de la fase arriba, que no depende de
"atraso" sino de una desalineación estructural) sigue sin confirmarse.

### Teoría de desalineación de fase: TAMBIÉN descartada (2026-11-20)

Se instrumentó temporalmente cada lectura del Z80 al buffer (`$1B40`-`$1B7F`)
junto con los valores de `rptr`/`wptr` en ese instante. Resultado:
**el consumo está perfectamente sincronizado** — el puntero de lectura
siempre alcanza al de escritura, leyendo los pares `$FF`/valor exactamente
como se depositan, incluso justo después de pasar por el wrap (última
lectura capturada: `fifo[1B40]=0C rptr=01 wptr=01` — lectura y escritura
coinciden exacto, sin ningún atraso ni corrupción visible).

**Esto descarta la teoría de desalineación de fase.** El buffer y su
lectura están sanos. El problema real es otro: **el 68K deja de intentar
depositar CUALQUIER comando nuevo** (no solo este cue periódico en
particular) — consistente con el silencio total y permanente que mide el
análisis de audio (no es "a este objeto/efecto puntual se le acaba el
cue", sería raro que TODA la música también se detenga a la vez por esa
razón sola).

**Hipótesis actual, sin confirmar**: algo parecido a una bandera global de
"driver ocupado/bloqueado" en el lado 68K, que debería ser momentánea
(protegiendo una sección crítica breve) pero queda trabada después de este
evento puntual, impidiendo que CUALQUIER código posterior intente
encolar un comando nuevo — en vez de un problema del buffer en sí.
Requiere encontrar ese posible flag/candado compartido por todos los
puntos de entrada `$1CB5A0`/`$1CB5B0`/etc. (no solo `$1CB632`/`$1CB640`),
no seguir mirando el buffer o su consumo, que ya están confirmados sanos.

### Encontrado: la bandera `$FFFFBFD6` dispara el cue, pero deja de recibir el reset (2026-11-20)

Se confirmó con `sega2asm` + lectura directa del 68K recompilado que
`$1E47B4`-`$1E47CA` es un chequeo con 3 condiciones (`btst #7,($FFFFC18A)`,
`tst.b ($FFFFC191)`, `tst.b ($FFFFBFD6)`) antes de llamar a `$1E4944`
(que dispara nuestro cue `$0C` y marca `$FFFFBFD6=$FF`). Dos sitios
distintos (`$1E4660`, `$1E499C`) hacen `sf ($FFFFBFD6)` (lo limpian a 0)
para rearmar el ciclo.

Instrumentando cada escritura a `$FFFFBFD6` (vía el camino inline en
`recomp_rt.h`, no `bus_write8` — el código nativo no pasa por ahí):

```
frame=1502 write=FF   frame=1651 write=00
frame=1802 write=FF   frame=1951 write=00
frame=2102 write=FF   frame=2251 write=00
...                   ...
frame=3602 write=FF   frame=3751 write=00
frame=3902 write=FF   ← nunca más se escribe nada después de esto
```

**El patrón es perfecto durante 9 ciclos completos (~150 frames cada uno)
y se corta exactamente en el mismo frame de siempre.** Esto descarta
definitivamente que sea un problema del buffer, del Z80, o de la bandera
en sí (otro valor, otra dirección compartida, etc.) — es que **el objeto
o temporizador que maneja TODO este ciclo (el disparo Y el reset) deja de
ser actualizado en absoluto** a partir de este punto. No es que falle
una vez: deja de ejecutarse por completo.

`$FFFFC191` se escribe una sola vez (frame 1217, init) — no cicla, no
parece relevante al ciclo en sí.

**Esto es ahora el límite real de lo investigado esta sesión.** El
siguiente paso sería encontrar qué código llama a la función que contiene
este chequeo ($1E47B4 en adelante) — probablemente el "tick" de algún
objeto o temporizador global — y entender por qué deja de ser invocado.
Requiere expandir el desensamblado 68K (no solo confiar en los comentarios
del código recompilado) para seguir la cadena de llamadas hacia arriba.

### CAUSA RAÍZ MÁS PROBABLE ENCONTRADA: bug documentado oficialmente de GEMS (2026-11-20) — SUPERADO, ver parte 2 arriba

**Actualización (misma fecha, sesión posterior)**: esta hipótesis nunca se
confirmó con una traza real de ejecución (quedó pendiente, ver abajo). La
traza instrucción-por-instrucción tomada después muestra que el Z80 NO
está en `seqcmdloop`/`seqgoto` cuando se cuelga, sino en `DACME` (el
reproductor de PCM) — un subsistema distinto. Se deja todo el
razonamiento de esta sección como registro histórico (y porque el loop
GOTO/LABEL de duración cero SIGUE siendo un bug real y documentado de
GEMS, solo que no es el que está pasando acá), pero **no seguir esta
pista** sin evidencia nueva. Ver la sección nueva "`DACME`: hallazgo
confirmado con traza real" más abajo.

El usuario encontró y descargó el **kit de desarrollo oficial de GEMS
(release 2007, de Recreational Brainware/Sega, vía Hidden Palace)**,
guardado en `Tools/GEMS-SDK-2007/` — incluye `GEMS.DOC` (manual del
compositor), `README.DOC` (guía para programadores, versiones 2.2-2.5), y
el código fuente REAL `GEMS/GEMS/Z80.ASM`, `GEMS/GEMS/GEMS.A` (rutinas 68K).

`README.DOC`, sección "Release Notes from 2.2" → "Warnings":

> **"Zero duration loops** - Be careful to avoid any GEMS sequencer loops
> having 0 duration (a LOOP START followed by a LOOP END on the same
> tick). **This will hang the GEMS sequencer.** Though you are unlikely
> to create such a case, it can get tricky when using GOTO and LABEL -
> a GOTO transfers the sequencer to the LABEL opcode with 0 delay."

**Esto es un bug documentado por los propios autores de GEMS desde 1991**,
no algo nuestro ni exclusivo de Comix Zone. El mismo documento (vía
`GEMS.DOC`) confirma que las secuencias pueden usar los opcodes
`GOTO`/`LABEL` (parte de los "Conditional Opcodes" agregados en v2.0)
para implementar loops — exactamente el tipo de construcción que
produciría nuestro patrón observado: un cue que se repite cada ~150
frames, alternando dos valores (`$0C`/`$0D`), consistente con un pequeño
loop de secuencia (play → espera → GOTO al inicio).

**Encaja con TODO lo que confirmamos empíricamente:**
- "Cuelga el secuenciador" (de ESA secuencia/canal específico), no el Z80
  entero — consistente con que el resto del driver (lectura del FIFO,
  otros canales) siguiera funcionando sano, que ya confirmamos.
- Pasa después de una cantidad fija de repeticiones (9 ciclos limpios,
  se cuelga en el 10°) — consistente con un acumulado de redondeo en el
  cálculo de duración (tempo-escalado) que eventualmente da 0 en una
  iteración específica, no en la primera.
- Determinístico por tiempo/frame, no por acción del jugador — es un
  bug en los DATOS de la secuencia musical del ROM (cómo el compositor
  armó el loop), no en código que dependa de input.

**Confirmado en el código real** (`Z80.ASM` oficial, etiqueta `seqgoto`):
tras actualizar el puntero de la secuencia con el offset del GOTO, la
instrucción siguiente es `jp seqcmdloop0` — vuelve a procesar el
siguiente comando **inmediatamente, sin ningún paso de espera/duración
de por medio**. Si el LABEL de destino encadena a otro GOTO (o a un
comando que no implica espera), el Z80 queda procesando ese mini-loop
para siempre, SIN volver nunca al resto del ciclo principal del driver
(que es lo que revisa la cola de comandos para CUALQUIER otro sonido,
no solo esta secuencia). Esto reconcilia todo lo medido: el PC variando
entre un puñado de direcciones (`$0041`-`$0FA7`) es ese mini-loop
repitiéndose, y por qué se silencia TODO el sonido, no solo este cue
puntual — el driver entero queda atascado esperando que ESTA secuencia
termine su tick, cosa que nunca pasa.

**Esto sería entonces un bug genuino de los DATOS de música del ROM
original de 1995** (un loop GOTO/LABEL con duración cero en alguna
secuencia de ambiente), reproducible en hardware real, no introducido
por la recompilación. Arreglarlo de verdad requeriría editar los BYTES
de la secuencia musical en el ROM (no el motor ni el código 68K) —
territorio de edición de datos GEMS, para el cual ahora sí tenemos las
herramientas reales (`GEMS/GEMS/Z80.ASM` para entender el formato exacto
de opcodes, y potencialmente `realmonster/GEMS` o `GEMSPlay` para
parsear/editar el banco de secuencias de Comix Zone directamente).

### Hallazgo más preciso hasta ahora: el Timer A del chip FM (2026-11-20) — contexto parcial, ver `DACME` abajo

**Actualización**: el "re-armado del Timer A que deja de ocurrir" descrito
acá es un síntoma correcto, pero la hipótesis de "bug sutil de re-armado
en `ymfm`/nuestro wrapper" de esta sección se descartó: una traza
incondicional del puerto de status del YM2612 (ver `DACME` abajo) muestra
que el bit de overflow del Timer A se lee **casi siempre en 1 desde el
frame 100** (no es algo que empiece a fallar recién en el frame 3519) —
es decir, nuestro timer YA corría "siempre desbordado" desde temprano, sin
que eso rompiera nada audible durante ~59 segundos. El re-armado real
(escribir `$27`) ES la rutina `DACME`, y el motivo de que dejara de
ocurrir fue simplemente que `DACME` quedó en loop infinito sin volver
nunca a su punto de llamada (no un problema del propio timer). Ver
detalle completo abajo.



Confirmado con instrumentación directa que Comix Zone **sí usa el Timer A
del YM2612** activamente (algo que no todos los juegos GEMS hacen — el
motor compartido `segagenesisrecomp`, de otra comunidad de recompilación,
tiene un issue cerrado, #83, agregando soporte de Timer A precisamente
porque "no todos los drivers" solo necesitan el Timer B que modelaban
originalmente).

Patrón observado (registros `$24`/`$25`=periodo, `$27`=control):
```
$27=$15 (carga completa + reset) cada ~30-80 frames
$27=$05 (ack simple, sin reset)  cada ~10-20 frames
```
perfectamente regular desde el arranque — **hasta el frame 3519, donde
se corta para siempre**, en el mismo punto exacto donde el secuenciador
completo y el PC del Z80 (que deja de ciclar en el rango normal) se
congelan. Esto es más preciso que el hallazgo anterior (el tempo/beat
counter genérico): es específicamente el **re-armado del Timer A** lo
que deja de ocurrir.

**Hipótesis actual (sin confirmar del todo)**: el mecanismo de "tick" de
GEMS sondea el bit de desborde del Timer A (no usa interrupciones Z80
para esto — confirmado que nuestro motor NUNCA dispara una interrupción
del Z80 por el timer del FM, solo por VBlank, y aun así la música
funciona bien los primeros ~60 segundos, así que GEMS debe sondear el
estado directamente). El patrón de escrituras (`$15` completo intercalado
con varios `$05` de solo-ack) es exactamente el caso sutil que el issue
#83 de `segagenesisrecomp` documenta como propenso a bugs de re-armado
("un ack simple, sin bit de reset, con el mismo período, debe dejar la
fase de un timer corriendo intacta" — si `ymfm` o nuestro wrapper no
respetan esto exactamente bajo esta secuencia específica de llamadas,
el timer podría dejar de desbordar/contarse después de suficientes
ciclos de este patrón).

**No se llegó a confirmar/descartar esto con un fix verificado** — es la
pista más prometedora y precisa que tenemos, pero falta: instrumentar
directamente el estado interno del timer en `ymfm` (no solo los
registros que le escribimos) para ver si dentro de la librería el timer
efectivamente deja de contar, o si el problema está en cuántas veces
`tick_timers()` lo alimenta con ciclos reales.

### `DACME`: hallazgo confirmado con traza real (2026-11-20, sesión posterior)

Repro usada: **cero input**, `GEMS_DEBUG=1 ./ComixZone ComixZone_U.bin
--headless 3545`. No hace falta ningún script de botones — confirma lo
que la parte 1 ya sabía (el bug es independiente del jugador).

**Dirección local `$03B0`** (Z80, = ROM `$37E2+$3B0` = `$3B92`) es
**`DACME`** del driver GEMS real — confirmado byte a byte contra
`Tools/GEMSPlay/asm-sources/Z80_2-8_cleaned.ASM:460-526`, en particular su
trampolín auto-modificable característico:
```
DACME   jr DACMEALT      ; se auto-parchea a EXX ($D9)/EX AF,AF' ($08)
                         ; la primera vez que corre (comentario original:
                         ; "change to EXX/EX AF to enable, RET to disable")
        ld (HL),27H      ; apunta el FM al registro de control de Timer A
dacmespin:
        bit 0,(HL)       ; esperá a que el Timer A desborde
        jp Z,dacmespin
        inc L
        ld (HL),B        ; resetea el timer (además selecciona modo CH3)
        dec L
        ld A,(DE)        ; siguiente byte del FIFO de muestras PCM
DACMEPROC: jr DACMEDSP    ; (decodifica nibble alto/bajo si está comprimido)
DACMEOUT: ld (HL),02AH   ; apunta al registro de datos DAC
        inc HL
        ld (HL),A        ; escribe la muestra
        dec L
        ex AF,AF' \ exx
DACMERET: ret            ; única salida en el código de referencia
```
Encontrado inicialmente al ver que, tras el VBlank ISR del frame 3534, el
Z80 **no vuelve a ejecutar ningún CALL ni RET** — solo un puñado de
direcciones (`$03E5`-`$046C`) ciclando para siempre vía JP/JR puros (ver
"Herramientas nuevas" abajo, detección automática de cuelgue). Una traza
de 400 instrucciones confirmó, en orden, exactamente la secuencia de
arriba — incluidas las DOS rutas alternadas de decodificación de nibble
(`DACME4BHI`/`DACME4BMSK` del original), repitiéndose sin fin.

**Diferencia real con el código de referencia (2.8)**: en la referencia,
`DACMERET: ret` se ejecuta **una vez por llamada** — el llamador decide
cuándo volver a invocar `DACME`. En este build de Comix Zone, el camino
nunca llega a ningún `ret`: en vez de eso hay un JP/JR que vuelve al tope
(`$0435`) — es decir, **esta variante procesa un lote de muestras por
llamada en un loop interno**, no una sola. El bug está en la condición
que debería cortar ese loop (probablemente "¿está vacío el FIFO de
muestras PCM? si sí, volvé") — nunca se observó que se cumpla en la
corrida que se cuelga.

**Confirmado con traza incondicional del puerto YM2612** (nueva, ver
abajo) que el patrón de escrituras por iteración es exactamente el de
arriba: `$4001←$15` (reset timer), `$4000←$2A` (selecciona DAC), `$4001←XX`
(muestra real, valores distintos cada vez: `$8E`, `$9C`, `$95`, ... — son
datos de audio genuinos, no basura), `$4000←$27`+lectura de status
(`$81`=busy+overflow) antes de decidir el salto de vuelta.

**Dato que descarta una hipótesis tentadora**: el bit de overflow del
Timer A (bit 0 del status) se lee como "siempre en 1" **desde el frame
100**, no desde el frame del cuelgue — `DACME` lleva corriendo así,
"a toda velocidad" sin la pausa de sincronización que el hardware real
impondría, durante los ~59 segundos en que el audio sonó perfecto. Esto
**no es la causa del cuelgue** (es una imprecisión de timing preexistente
e inofensiva, al menos a esta escala) — es una pista que llevaría a una
investigación equivocada si se la persigue.

**Próximo paso concreto** (no completado esta sesión): el FIFO de
muestras PCM de `DACME` es un buffer separado del de comandos
(`$1B40`-`$1B7F`, ya resuelto) — según el código de GEMS vive en
`$1F00`-`$1FFF` (`DACFIFOWPTR`/`SAMPLEPTR` en el `.ASM` de referencia,
rellenado desde el 68K vía `FILLDACFIFO`/`GETSBYTE`). Hace falta: (1)
identificar los punteros de lectura/escritura reales de ESTE build (no
necesariamente las mismas direcciones que la referencia 2.8), (2)
comparar su estado en la llamada que se cuelga contra llamadas anteriores
exitosas, (3) mirar si conecta con el hallazgo ya confirmado de la parte 1
de que algo del lado 68K (la bandera `$FFFFBFD6`) deja de tickearse en
este mismo instante — sería el candidato más directo para "quién debería
estar rellenando este FIFO y dejó de hacerlo".

### Herramienta de debug persistente (`GEMS_DEBUG=1`) — 2026-11-20

En vez de agregar/revertir instrumentación en cada sesión de investigación
(doble trabajo), quedó una herramienta permanente en el motor, sin costo
cuando está apagada:

```bash
GEMS_DEBUG=1 ./ComixZone <rom> [--headless N | ventana real]
```

Con la variable seteada, imprime por `stderr`:
- `[dbg] frame=N YM keyon/off data=XX (chan C, ops O)` — cada nota
  encendida/apagada en el chip FM.
- `[dbg] frame=N FM reg=XX data=YY` — escrituras a los registros del
  Timer A (`$24`/`$25`/`$27`).
- `[dbg] frame=N write BFD6=XX` — escrituras a la bandera guardia
  `$FFFFBFD6` (específica de Comix Zone).
- `[dbg] frame=N z80 reads fifo[XXXX]=YY rptr=RR wptr=WW` — cada lectura
  del buffer circular de comandos de sonido (`$1B40`-`$1B7F`).
- `[dbg] frame=N z80.pc=XXXX` — una muestra del PC del Z80 por frame.
- `[dbg] frame=N z80 CALL depth=D pc=PP op=OO -> target=TT sp=SS` / `...
  z80 RET  depth=D pc=PP -> target=TT sp=SS` (agregado 2026-11-20, sesión
  posterior) — profundidad de pila reconstruida de CALL/RST/RET/RETI/RETN
  reales (clasificando el opcode, no solo mirando el SP, para no confundir
  PUSH/POP de registros con llamadas). Incluye las entradas a interrupción
  (vectores `$0038`/`$0066`) aunque hayan quedado "pegadas" al mismo
  `z80_step` que la instrucción anterior.
- `[dbg] frame=N z80 HANG? no CALL/RET for 2 frames, dumping next 400
  instructions` seguido de `[dbg] frame=N z80 HANGSTEP pc=PP op=OO
  op2=O2 -> pc'=QQ sp'=SS` (agregado 2026-11-20, sesión posterior) —
  **se dispara solo**, sin saber de antemano el frame del cuelgue: si
  pasan 2 frames seguidos sin ningún CALL/RET, vuelca las siguientes 400
  instrucciones PC-a-PC (acotado porque el cuelgue real es infinito) y se
  rearma para la próxima vez que vuelva a pasar. Así se encontró `DACME`
  (ver sección dedicada arriba).
- `[dbg] frame=N z80 YM status=XX` — **cada** lectura del puerto de status
  del YM2612 (no filtrada), con el valor devuelto. `[dbg] frame=N z80 YM
  port=AAAA data=XX` — **cada** escritura a cualquier puerto del YM2612
  (dirección Z80 real, no solo los registros que `sound.cpp` ya reconocía
  por su cuenta). Ambas agregadas 2026-11-20, sesión posterior — más caras
  que el resto (una línea por acceso al chip FM), pero fueron las que
  permitieron ver que `DACME` sí escribe al puerto DAC correctamente.
- `[dbg] frame=N z80 DACME entry de=DDDD hl=HHHH bc=BBBB a=AA
  mem03e9=XX mem03ee=YY` — snapshot de registros y de dos celdas de RAM
  sospechosas (agregado 2026-11-20, sesión posterior, hardcodeado a la
  dirección `$03B0`, específico de Comix Zone como el resto de las trazas
  de este estilo) en **cada** entrada a esa subrutina compartida, sin
  importar quién la llame. Clave para encontrar que la llamada que se
  cuelga es la primera vez en toda la corrida que ese caller (`$0699`)
  pasa `hl`/`bc` distintos de `0000`/`0003` (ver parte 3 arriba).

Sin la variable: cero líneas, cero costo (un solo chequeo de entero
cacheado por llamada). Implementado en `genesis.h` (declaraciones,
`g_dbg_frame`/`gems_debug_on()`), `machine.c` (definiciones),
`sound.cpp`, `z80bus.c`, `recomp_rt.h` (los puntos de instrumentación).

`--dump` también incluye ahora, siempre, los 8KB de RAM del Z80 al final
del archivo (antes había que agregarlo a mano cada vez) — offset
`65536+65536+128+80+32` bytes dentro del dump, 8192 bytes de largo; el
array CCB vive en el offset `0x1B80` de esa región, 32 bytes por slot,
16 slots (ver layout completo arriba).

### Próximos pasos si se retoma

1. **Buscar un candado/flag global compartido** por TODOS los puntos de
   entrada de depósito (no solo `$1CB632`/`$1CB640`) que pueda quedar
   trabado después del evento del frame 3902 — es la hipótesis actual
   tras descartar tanto el desborde del buffer como la desalineación de
   fase (ver sección arriba). Buscar qué chequea cada entry point ANTES
   de siquiera llamar a `$1CB53E` — probablemente en el código que
   decide "hay espacio/puedo encolar" en el propio llamador, no en las
   rutinas de depósito que ya confirmamos sanas.
2. Revisar si hay alguna fuente de interrupciones del Z80 (timers del
   chip FM vía `sound.cpp`/`tick_timers`, u otra) que podría relacionarse
   con que el 68K dejara de intentar depositar — aunque el foco ahora
   debería estar en el lado 68K, no en el Z80 (ya confirmado sano).
3. **Identificar qué cue periódico es este** — seguir la pista que
   dejamos a medio camino: qué rutina del 68K en `recomp_14.c` (alrededor
   de `$1E4944`-`$1E49D0`) llama a `$1CB632`/`$1CB640` cada 2.5s;
   probablemente un efecto ambiental o un tick de música de fondo.
4. **Decidir si parchear el ROM original** (no el motor) para hacer el
   parser robusto a la desalineación — esto es una modificación
   deliberada del juego original, no una corrección de recompilación, y
   amerita una decisión explícita antes de aplicarla.
5. Expandir este proyecto de descompilación (`comixzone.yaml`) para cubrir
   más del ROM, empezando por la rutina `$1CB53E`-`$1CB650` del lado 68K.
