#!/usr/bin/env bash
# Builds the «Hola ESP» standby app (standby/, docs/WAKEWORD.md), packs its model and flashes both:
#   app1   (0x650000) <- standby/.pio/build/standby/firmware.bin
#   spiffs (0xc90000) <- standby/.pio/build/standby/srmodels/srmodels.bin
# Nothing else is written: bootloader, partition table, otadata, NVS (settings) and app0 (the TV) stay.
# The TV firmware must be a voice_ww / voice_nokeys_ww build to hand STANDBY VOZ over to it.
#
#   tools/standby_flash.sh [port]      # default /dev/cu.usbmodem101
#   tools/standby_flash.sh --build     # build and pack only
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SB="$ROOT/standby"
BUILD="$SB/.pio/build/standby"
PY="$HOME/.platformio/penv/.espidf-5.4.1/bin/python"  # the ESP-IDF 5.4.1 environment PlatformIO made (movemodel.py)
ARDUINO_TABLE="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/default_16MB.csv"

# The TV's table and the app's copy must be the same, or the offsets below would write over something else.
cmp -s "$SB/partitions.csv" "$ARDUINO_TABLE" || { echo "standby/partitions.csv differs from the TV's default_16MB.csv" >&2; exit 1; }
part() { awk -F, -v n="$1" '$1 ~ "^ *"n" *$" { gsub(/ /, "", $4); gsub(/ /, "", $5); print $4, $5 }' "$SB/partitions.csv"; }
read -r APP1_AT APP1_SIZE <<<"$(part app1)"
read -r MODEL_AT MODEL_SIZE <<<"$(part spiffs)"

# PlatformIO does not regenerate the linker script when sdkconfig.defaults changes: a cache size change then
# links IRAM where the cache now lives and the app dies at boot (2026-10-03). Start clean after any change.
if [ "$SB/sdkconfig.defaults" -nt "$BUILD/memory.ld" ]; then rm -rf "$BUILD" "$SB/sdkconfig.standby"; fi
pio run -d "$SB"
"$PY" "$SB/managed_components/espressif__esp-sr/model/movemodel.py" \
  -d1 "$SB/sdkconfig.standby" -d2 "$SB/managed_components/espressif__esp-sr" -d3 "$BUILD" | grep -E "^\s+- |Recommended"

APP="$BUILD/firmware.bin"
MODEL="$BUILD/srmodels/srmodels.bin"
[ -f "$APP" ] && [ -f "$MODEL" ] || { echo "missing $APP or $MODEL" >&2; exit 1; }
fits() { [ "$(stat -f%z "$1")" -le $(($2)) ] || { echo "$1 does not fit in $2 bytes" >&2; exit 1; }; }
fits "$APP" "$APP1_SIZE"
fits "$MODEL" "$MODEL_SIZE"
echo "app $(stat -f%z "$APP") B -> app1 $APP1_AT, model $(stat -f%z "$MODEL") B -> spiffs $MODEL_AT"
[ "${1:-}" = "--build" ] && exit 0

pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port "${1:-/dev/cu.usbmodem101}" --baud 921600 write_flash \
  --flash_mode keep --flash_freq keep --flash_size keep "$APP1_AT" "$APP" "$MODEL_AT" "$MODEL"
