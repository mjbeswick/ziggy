#!/usr/bin/env python3
"""Ask the board for a screenshot and save it as a PNG.

    uv run --with pyserial --with pillow scripts/shot.py out.png

The firmware renders the active LVGL screen into a PSRAM buffer and writes
`SHOT <w> <h> RGB888 <stride>` followed by raw pixels on the USB serial
console.
"""
import sys
import time

import serial
from PIL import Image

PORT = "/dev/cu.usbmodem1401"


def read_exact(ser, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError(f"wanted {n} bytes, got {len(buf)}")
        buf += chunk
    return bytes(buf)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "shot.png"
    ser = serial.Serial(PORT, 115200, timeout=5)
    # Opening the port resets the board; wait for the app console.
    deadline = time.time() + 12
    while time.time() < deadline:
        if b"bumble console" in ser.readline():
            break
    time.sleep(0.5)
    ser.reset_input_buffer()
    ser.write(b"shot\n")
    ser.flush()

    deadline = time.time() + 20
    header = None
    while time.time() < deadline:
        line = ser.readline()
        if line.startswith(b"SHOT "):
            header = line.decode(errors="replace").split()
            break
        if b"SHOT FAILED" in line:
            print("firmware could not snapshot", file=sys.stderr)
            return 1
    if not header:
        print("no SHOT header", file=sys.stderr)
        return 1

    _, w, h, fmt, stride = header
    w, h, stride = int(w), int(h), int(stride)
    bpp = {"RGB565": 2, "RGB888": 3}[fmt]
    raw = read_exact(ser, h * stride)
    ser.close()

    if bpp == 2:
        px = bytearray(w * h * 3)
        for i in range(w * h):
            y, x = divmod(i, w)
            off = y * stride + x * 2
            v = raw[off] | (raw[off + 1] << 8)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            px[3 * i] = (r << 3) | (r >> 2)
            px[3 * i + 1] = (g << 2) | (g >> 4)
            px[3 * i + 2] = (b << 3) | (b >> 2)
        img = Image.frombytes("RGB", (w, h), bytes(px))
    else:
        px = bytearray(w * h * 3)
        for y in range(h):
            row = raw[y * stride : y * stride + w * 3]
            px[y * w * 3 : (y + 1) * w * 3] = row
        img = Image.frombytes("RGB", (w, h), bytes(px))

    img.save(out)
    print(f"{out}: {w}x{h} {fmt}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
