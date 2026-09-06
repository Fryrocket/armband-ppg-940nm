#!/usr/bin/env bash
# Flash Armband_Full.ino to the Seeed XIAO ESP32C3 on USB CDC.
# Usage: ./scripts/flash_xiao.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN="esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default,UploadSpeed=921600"
PORT="${1:-}"
# Arduino-CLI requires the folder name to match the .ino basename.
SKETCH="$(mktemp -d /tmp/xiao-flash.XXXX)/Armband_Full"
mkdir -p "$SKETCH"
cleanup() { rm -rf "$(dirname "$SKETCH")"; }
trap cleanup EXIT

if [[ -z "$PORT" ]]; then
  PORT="$(arduino-cli board list --format json 2>/dev/null | python3 -c '
import json,sys
data=json.load(sys.stdin)
detected=data.get("detected_ports") or data.get("ports") or []
# arduino-cli 1.x: {detected_ports:[{port:{address,...}, matching_boards:[...]}]}
cands=[]
items=detected if isinstance(detected, list) else []
if not items and isinstance(data, dict):
    items=data.get("serial_ports") or []
for item in items:
    port=(item.get("port") or item)
    addr=port.get("address") or ""
    boards=item.get("matching_boards") or []
    if "usbmodem" in addr or boards:
        cands.append(addr)
print(cands[0] if cands else "")
' || true)"
fi
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No XIAO serial port. Plug in the Seeed XIAO ESP32C3 (USB-C) and retry." >&2
  arduino-cli board list >&2 || true
  exit 1
fi

mkdir -p "$SKETCH"
cp "$ROOT/firmware/Armband_Full.ino" "$SKETCH/Armband_Full.ino"
if [[ -f "$ROOT/firmware/secrets.h" ]]; then
  cp "$ROOT/firmware/secrets.h" "$SKETCH/secrets.h"
  echo "Using firmware/secrets.h"
else
  echo "No firmware/secrets.h — WiFi/MQTT stay placeholders. Copy secrets.h.example to secrets.h."
fi

ESPBLE="${HOME}/Library/Arduino15/packages/esp32/hardware/esp32/3.3.11/libraries/BLE"
echo "FQBN=$FQBN"
echo "PORT=$PORT"
arduino-cli compile --fqbn "$FQBN" --library "$ESPBLE" "$SKETCH"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"
echo "Flash OK. Serial at 115200:"
echo "  arduino-cli monitor -p $PORT -c baudrate=115200"
