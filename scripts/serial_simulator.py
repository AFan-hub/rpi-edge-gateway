#!/usr/bin/env python3
"""Feed protocol frames into a serial device to exercise the gateway parser.

Usage:
    python3 serial_simulator.py /dev/pts/3 [--frames 200] [--corrupt 0.05]

Writes are randomly split (half packets) or deferred so two frames arrive in
one read (sticky packets); --corrupt flips one payload byte in a fraction of
frames to produce CRC errors. Use with a socat pty pair:

    socat -d -d pty,raw,echo=0 pty,raw,echo=0
    ./edge_gateway /dev/pts/4 &
    python3 serial_simulator.py /dev/pts/5 --frames 300 --corrupt 0.02
"""
import argparse
import random
import struct
import time


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes) -> bytes:
    body = bytes([0x01, cmd]) + struct.pack("<H", len(payload)) + payload
    return b"\xaa\x55" + body + struct.pack("<H", crc16_ccitt_false(body))


def sensor_payload(ts_ms: int, s1: float, s2: float) -> bytes:
    return struct.pack("<IffH", ts_ms, s1, s2, 0)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("device")
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("--corrupt", type=float, default=0.0,
                    help="probability of flipping one payload byte per frame")
    ap.add_argument("--period", type=float, default=0.05)
    args = ap.parse_args()

    random.seed(7)
    sent = 0
    pending = b""

    with open(args.device, "wb", buffering=0) as dev:
        for i in range(args.frames):
            frame = bytearray(build_frame(
                0x01, sensor_payload(i * 10, 20.0 + i * 0.1, 40.0)))

            # Corrupt AFTER the CRC is computed: flipping a payload byte
            # before build_frame() would just produce a valid frame with
            # wrong values, and the receiver could never detect it.
            if random.random() < args.corrupt:
                idx = random.randrange(6, 6 + 14)  # payload region only
                frame[idx] ^= 0xFF

            pending += bytes(frame)
            sent += 1

            if random.random() < 0.7 or i == args.frames - 1:
                # pending may already hold more than one frame (sticky packet)
                if len(pending) > 1 and random.random() < 0.5:
                    cut = random.randrange(1, len(pending))
                    dev.write(pending[:cut])
                    time.sleep(0.002)
                    dev.write(pending[cut:])
                else:
                    dev.write(pending)
                pending = b""

            time.sleep(args.period)

    print(f"sent {sent} frames to {args.device}")


if __name__ == "__main__":
    main()
