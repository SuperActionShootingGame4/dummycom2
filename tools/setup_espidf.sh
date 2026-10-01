#!/usr/bin/env bash
# Local (no-sudo) ESP-IDF v5.4 setup for ESP32 (WROOM) firmware builds.
set -euo pipefail
log(){ echo "[esp-idf setup $(date +%H:%M:%S)] $*"; }

export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-/home/subcon/.espressif}"
ESPDIR=/home/subcon/esp
mkdir -p "$ESPDIR"

if [ ! -d "$ESPDIR/esp-idf" ]; then
  log "cloning esp-idf v5.4 (recursive, shallow)"
  git clone -b v5.4 --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/espressif/esp-idf.git "$ESPDIR/esp-idf"
fi

cd "$ESPDIR/esp-idf"
log "running install.sh esp32 (toolchain -> $IDF_TOOLS_PATH)"
./install.sh esp32

log "DONE. Use: export IDF_TOOLS_PATH=$IDF_TOOLS_PATH ; . $ESPDIR/esp-idf/export.sh"
touch "$ESPDIR/.setup_complete"
