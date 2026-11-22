# Comix Zone — build Android (spike)

Estado: **compila, instala y corre el juego real** — probado en un TV box
Xiaomi Mi TV (MiTV-AFKR0, Amlogic sc2, Android 11, `armeabi-v7a` puro) por
adb/red. Funciona pero lento (SoC de 32-bit gama baja — esperable; no
probado en un dispositivo más potente todavía). Sigue sin controles
táctiles (la UI es la del motor desktop, pensada para teclado/control).

## Qué se tocó en `source/` para que compile en Android

- `source/CMakeLists.txt`: cuando `ANDROID` está definido (lo pone el
  toolchain file del NDK), compila una librería compartida `main` en vez
  de un ejecutable — es lo que `SDLActivity` (Java) carga por JNI. En
  desktop/Windows el comportamiento no cambió (sigue siendo el ejecutable
  de siempre).
- `source/CMakeLists.txt`: `m68kmake` (el generador de `m68kops.c`, un
  codegen que corre durante el build) se compila con el compilador del
  **host** cuando `CMAKE_CROSSCOMPILING` está activo, no con el toolchain
  cruzado — si no, queda un binario ARM64 que la PC x86 no puede ejecutar
  durante el propio build.
- `source/runtime/src/main.c`: `SDL_GetBasePath()` no existe en Android
  (SDL lo marca `SDL_Unsupported()` y devuelve `NULL` a propósito, porque
  "al lado del ejecutable" no tiene sentido ahí). Se agregó una rama
  `#ifdef __ANDROID__` que usa `SDL_AndroidGetInternalStoragePath()` en su
  lugar — sin esto, `settings.ini`, los save states y las fuentes se
  intentaban leer/escribir en la raíz `/`, de solo lectura.
- `source/local.json`: el ROM apuntaba a la ruta vieja y abandonada
  `GenesisRecomps/ComixZone/rom/...` (ver nota del CLAUDE.md de la raíz).
  Se corrigió a `comixzone-decomp-recomp/rom/ComixZone_U.bin`, el árbol
  activo. Esto no tiene nada que ver con Android — estaba roto antes
  también, solo que el build de desktop nunca lo notaba porque ya tenía
  `source/generated/comixzone/` en caché de una corrida anterior.
- `source/runtime/src/main.c`: si no hay `rom_path` en `settings.ini` (no
  hay argv en Android), busca el ROM en `/sdcard/ComixZone/ComixZone_U.bin`
  — ver la sección "El ROM en el dispositivo" más abajo, incluye por qué
  esa ruta puntual y no la carpeta propia de la app.

## El proyecto `android/`

Es el `android-project` template que trae SDL2 (`source/third_party/sdl2/
android-project`), copiado a `android/` y con dos cambios en
`android/app/build.gradle`:

- `externalNativeBuild.cmake.path` apunta directo a `../../source/
  CMakeLists.txt` (con `-DGAME=comixzone` como argumento) en vez de un
  `jni/CMakeLists.txt` propio — una sola fuente de verdad, nada
  duplicado.
- `abiFilters 'arm64-v8a'` solamente, por ahora (para iterar rápido). Para
  cubrir más dispositivos agregar `armeabi-v7a`.

Sí hubo que tocar un poco Java: `SDLActivity` por default intenta cargar
`libSDL2.so` como librería separada antes de `libmain.so` (pensado para
cuando SDL2 se compila como `.so` propio). Acá SDL2 está estático dentro
de `libmain.so` (`SDL_STATIC ON` en `source/CMakeLists.txt`), así que no
existe ese archivo y el `dlopen` fallaba apenas abría la app. Se agregó
`android/app/src/main/java/org/libsdl/app/ComixZoneActivity.java`, una
subclase de `SDLActivity` que sobreescribe `getLibraries()` para devolver
solo `{"main"}`, y el manifest apunta a esa clase.

## Compilar

Necesita: Android SDK + NDK (`ndk;28.2.13676358`, instalado vía
`sdkmanager`), y un JDK 17-21 (Gradle 8.9 no corre bien con JDK 25; en esta
máquina se usó el JDK que trae Android Studio, `/opt/android-studio/jbr`).

```bash
cd android
cat > local.properties <<EOF
sdk.dir=/home/shonidan/Android/Sdk
ndk.dir=/home/shonidan/Android/Sdk/ndk/28.2.13676358
EOF
export JAVA_HOME=/opt/android-studio/jbr
export PATH="$JAVA_HOME/bin:$PATH"
./gradlew assembleDebug
```

El APK queda en `android/app/build/outputs/apk/debug/app-debug.apk` (se
copió una copia a `android-build/ComixZone-debug.apk`, gitignoreada igual
que `source/build/` — es un derivado del ROM).

`source/local.json` tiene que tener el ROM configurado antes (lo normal
para compilar el motor para cualquier plataforma):
`node tools/setup.mjs --game comixzone /ruta/a/tu/ComixZone_U.bin`
desde `source/`.

## Instalar y probar

```bash
adb install -r android-build/ComixZone-debug.apk
```

### El ROM en el dispositivo

El recomp sigue necesitando el archivo ROM en tiempo de ejecución (los
datos — gráficos, niveles, sonido — no están recompilados, solo el
código 68000). En desktop se pasa como argumento (`./ComixZone rom.bin`);
Android no tiene argv de línea de comandos para una Activity lanzada
normalmente, así que hace falta dejarlo en un lugar fijo.

Dos intentos que **no** funcionaron en el TV box probado (Android 11,
scoped storage estricto):

- `adb shell run-as <pkg>` hacia el storage interno
  (`/data/data/<pkg>/files/` o `/data/user/0/<pkg>/files/`): SELinux lo
  deniega (dominio `runas_app` sin acceso), aunque el uid coincida.
- `adb push` directo a la carpeta externa propia de la app
  (`/sdcard/Android/data/<pkg>/files/`): el FUSE de storage con scope la
  bloquea para el proceso `adb`/`shell`, aun cuando la app ya la creó.

Lo que sí funciona — una carpeta pública, con el permiso "acceso a todos
los archivos" otorgado por `appops` (sin tocar la tele):

```bash
# una vez, después de instalar el APK:
adb shell appops set com.genesisrecomps.comixzone MANAGE_EXTERNAL_STORAGE allow

# cada vez que haga falta (re)poner el ROM:
adb shell mkdir -p /sdcard/ComixZone
adb push rom/ComixZone_U.bin /sdcard/ComixZone/ComixZone_U.bin
```

`main.c` busca ahí automáticamente si no hay `rom_path` guardado. Una vez
cargado una vez, el juego guarda `rom_path` en su propio `settings.ini`
(en el storage externo propio de la app) y no vuelve a necesitar el
fallback — pero como esa carpeta no es alcanzable por `adb push` en este
dispositivo, para reinstalar en limpio conviene dejar el ROM siempre en
`/sdcard/ComixZone/`.

Declarar `MANAGE_EXTERNAL_STORAGE` en el manifest + concederlo por
`appops` es records para desarrollo/sideload propio; no serviría para
publicar en Play Store tal cual (Google audita ese permiso).

### Pendiente — rendimiento

Probado en un Xiaomi Mi TV box (Amlogic sc2, `armeabi-v7a`, Android 11):
anda pero lento. Hipótesis a revisar antes de invertir en controles: el
renderer de software de SDL2 en ARMv7 sin NEON explícito, y el ~3-5% de
código que cae al intérprete Musashi en vez de estar recompilado. No
probado todavía en el Moto G ni la Tab A7 Lite (ambos arm64, fichas más
nuevas) — buen próximo paso para separar "el dispositivo es débil" de
"algo en el build de Android es ineficiente".

### Pendiente — controles

La UI actual es la de desktop (menú de pausa con Esc/Select, controles de
teclado/pad). En un teléfono sin control conectado no hay forma de tocar
nada todavía — falta una capa de D-pad/botones en pantalla. Service
aparte del spike de compilación.

### Sobre el 8BitDo M30

Conectado por USB-OTG (modo dongle 2.4G), Android debería verlo como un
gamepad HID genérico — el límite de 6 botones no es del SO sino de
`runtime/src/pads.c`/`io.c` (ver README de `source/`), así que lo más
probable es el mismo fallback a 3 botones que en PC por USB, salvo que se
porte esa lógica. No probado todavía.
