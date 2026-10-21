#!/usr/bin/env python3
"""Read-only measurement of NIGHTFALL LIVE terrain cadence and audio underruns."""
import argparse
import statistics
import time

from verify_live import Device, live


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="Felucca")
    parser.add_argument("--seconds", type=float, default=3.0)
    args = parser.parse_args()
    device = Device(args.port)
    changes = []
    polls = 0
    first_late = last_late = None
    previous_serial = None
    try:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            p = live(device)
            host_ms = time.monotonic() * 1000
            device_ms = int.from_bytes(p[1:5], "little")
            serial = p[-21]
            late = p[12] | p[13] << 8
            if first_late is None:
                first_late = late
            last_late = late
            polls += 1
            if serial != previous_serial:
                changes.append((host_ms, device_ms, serial, max(p[-20:])))
                previous_serial = serial
            time.sleep(0.025)
    finally:
        device.close()
    gaps = [b[1] - a[1] for a, b in zip(changes, changes[1:])]
    print(f"polls={polls} updates={len(changes)} audio_underruns={first_late}->{last_late}")
    if gaps:
        print(f"terrain interval ms: min={min(gaps)} median={statistics.median(gaps)} max={max(gaps)}")
    print("first/last rows:", changes[:3], changes[-3:])


if __name__ == "__main__":
    main()
