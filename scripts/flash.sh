#!/usr/bin/env bash
# Flash mojitalk, but only to the board we think we are talking to.
#
#   scripts/flash.sh            flash app + partition table + model
#   scripts/flash.sh app        app only (keep the model)
#
# The check is deliberate: this erases the model partition and the app.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${PORT:-}"
WANT_MAC="${WANT_MAC:-80:45:6b:33:cf:a8}"
MODE="${1:-all}"

source "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1

# USB Serial/JTAG ports renumber across resets, so find the board by MAC.
if [ -z "$PORT" ]; then
    for candidate in /dev/cu.usbmodem*; do
        [ -e "$candidate" ] || continue
        if python -m esptool --port "$candidate" flash_id 2>/dev/null | grep -qi "MAC: *$WANT_MAC"; then
            PORT="$candidate"
            break
        fi
    done
    if [ -z "$PORT" ]; then
        echo "no board with MAC $WANT_MAC on any /dev/cu.usbmodem* port" >&2
        exit 1
    fi
fi
echo "port: $PORT"

python -m esptool --port "$PORT" flash_id > /tmp/moji_flash_id.txt 2>&1 || {
    cat /tmp/moji_flash_id.txt
    echo "esptool could not talk to $PORT" >&2
    exit 1
}

if ! grep -q "Chip is ESP32-S3" /tmp/moji_flash_id.txt; then
    cat /tmp/moji_flash_id.txt
    echo "not an ESP32-S3 on $PORT" >&2
    exit 1
fi
if ! grep -qi "MAC: *$WANT_MAC" /tmp/moji_flash_id.txt; then
    cat /tmp/moji_flash_id.txt
    echo "wrong board: expected MAC $WANT_MAC" >&2
    exit 1
fi
if ! grep -q "Detected flash size: 16MB" /tmp/moji_flash_id.txt; then
    cat /tmp/moji_flash_id.txt
    echo "wrong flash size (expected 16MB)" >&2
    exit 1
fi

grep -E "Chip type|MAC|Detected flash" /tmp/moji_flash_id.txt

if [ "$MODE" = "app" ]; then
    python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
        write_flash 0x10000 build/ziggy.bin
    exit 0
fi

MODEL="${MODEL:-third_party/babytalk/models/citrinet256_int4.mmrt}"
[ -f "$MODEL" ] || { echo "missing model $MODEL" >&2; exit 1; }

python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0      build/bootloader/bootloader.bin \
    0x8000   build/partition_table/partition-table.bin \
    0x10000  build/ziggy.bin \
    0x510000 "$MODEL"
