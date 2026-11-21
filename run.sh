#!/usr/bin/env bash
# Lanza la recompilación nativa de Comix Zone en español.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

ROM="${1:-rom/ComixZone_U.bin}"
BIN="source/build/comixzone/ComixZone"

if [ ! -x "$BIN" ]; then
    echo "No se encontró $BIN. Compilá el proyecto primero (ver source/README.md):" >&2
    echo "  cd source && node tools/kit.mjs comixzone" >&2
    exit 1
fi

if [ ! -f "$ROM" ]; then
    echo "No se encontró la ROM en: $ROM" >&2
    exit 1
fi

exec "$BIN" "$ROM" --language spanish
