#!/usr/bin/env python3
"""Small NIGHTFALL v1 USB-MIDI post-flash smoke test.

Requires mido/python-rtmidi on the host. Only bounded, reversible parameter
writes are made; originals are restored in finally blocks.
"""
import argparse
import time

import mido


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def pack7(data):
    out, acc, bits = [], 0, 0
    for byte in data:
        acc |= byte << bits
        bits += 8
        while bits >= 7:
            out.append(acc & 127)
            acc >>= 7
            bits -= 7
    if bits:
        out.append(acc & 127)
    return out


def unpack7(data):
    out, acc, bits = [], 0, 0
    for byte in data:
        acc |= byte << bits
        bits += 7
        if bits >= 8:
            out.append(acc & 255)
            acc >>= 8
            bits -= 8
    return bytes(out)


class Device:
    def __init__(self, name):
        inputs = [n for n in mido.get_input_names() if name.lower() in n.lower()]
        outputs = [n for n in mido.get_output_names() if name.lower() in n.lower()]
        if len(inputs) != 1 or len(outputs) != 1:
            raise RuntimeError(f"ambiguous or missing MIDI port: in={inputs}, out={outputs}")
        self.input = mido.open_input(inputs[0])
        self.output = mido.open_output(outputs[0])
        self.sequence = 0

    def close(self):
        self.input.close()
        self.output.close()

    def request(self, command, payload=b""):
        sequence = self.sequence
        self.sequence = (sequence + 1) & 255
        body = bytearray([0x46, 0x4e, 1, command, sequence, len(payload), 0])
        body.extend(payload)
        body.extend(crc16(body).to_bytes(2, "little"))
        self.output.send(mido.Message("sysex", data=[0x7d, 0x4e, 0x46, 1, *pack7(body)]))
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            for msg in self.input.iter_pending():
                if msg.type != "sysex" or tuple(msg.data[:4]) != (0x7d, 0x4e, 0x46, 1):
                    continue
                data = unpack7(msg.data[4:])
                if len(data) < 9 or data[:3] != b"FN\x01" or data[3] != (command | 0x80) or data[4] != sequence:
                    continue
                length = data[5] | data[6] << 8
                if len(data) != length + 9 or crc16(data[:-2]) != int.from_bytes(data[-2:], "little"):
                    raise RuntimeError("response CRC or length mismatch")
                if data[7]:
                    raise RuntimeError(f"command {command} rejected: {data[7]}")
                return data[7:-2]
            time.sleep(0.005)
        raise TimeoutError(f"command {command} timed out")


def detail(device):
    data = device.request(17)
    if len(data) != 117:
        raise RuntimeError(f"unexpected DETAIL size {len(data)}")
    return data


def live(device):
    data = device.request(18)
    if len(data) < 75 or len(data) != 75 + 8 * data[53]:
        raise RuntimeError(f"unexpected LIVE size {len(data)}")
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="Felucca")
    args = parser.parse_args()
    device = Device(args.port)
    try:
        hello = device.request(1)
        if hello[1:10] != b"NIGHTFALL" or hello[10] != 1:
            raise RuntimeError("NIGHTFALL v1 handshake mismatch")
        before = detail(device)
        tempo = before[1] | before[2] << 8
        fx = before[111]
        fx_value = before[34 + 4 * fx]
        length = before[115]
        print(f"HELLO OK; tempo={tempo} fx={fx}:{fx_value} pattern={length} playing={before[3]}")
        changed = tempo + 1 if tempo < 240 else tempo - 1
        try:
            device.request(5, changed.to_bytes(2, "little"))
            if (detail(device)[1] | detail(device)[2] << 8) != changed:
                raise RuntimeError("tempo did not read back")
        finally:
            device.request(5, tempo.to_bytes(2, "little"))
        print("TEMPO write/read/restore OK")
        changed = fx_value + 1 if fx_value < 127 else fx_value - 1
        try:
            device.request(6, bytes([fx, 0, changed]))
            if detail(device)[34 + 4 * fx] != changed:
                raise RuntimeError("FX did not read back")
        finally:
            device.request(6, bytes([fx, 0, fx_value]))
        print("FX write/read/restore OK")
        changed = 32 if length != 32 else 16
        try:
            device.request(16, bytes([16, 0, 0, changed]))
            if detail(device)[115] != changed:
                raise RuntimeError("pattern length did not read back")
        finally:
            device.request(16, bytes([16, 0, 0, length]))
        print("PATTERN LENGTH write/read/restore OK")
        terrain_before = live(device)
        device.request(3, bytes([3, 60, 100]))
        try:
            time.sleep(0.16)
            terrain_during = live(device)
            events = [terrain_during[54 + 8 * i:62 + 8 * i]
                      for i in range(terrain_during[53])]
            if not any(event[1] == 60 and event[7] == 255 for event in events):
                raise RuntimeError("live note missing from visual snapshot")
            if terrain_during[-21] == terrain_before[-21] or not any(terrain_during[-20:]):
                raise RuntimeError("terrain did not update with sounding note")
        finally:
            device.request(4, bytes([3, 60]))
        print("NOTE ON/OFF and 20-band terrain snapshot OK; audio requires human listening")
        after = detail(device)
        if after[1:3] != before[1:3] or after[34 + 4 * fx] != fx_value or after[115] != length:
            raise RuntimeError("state restoration failed")
        print("RESTORED STATE verified; NIGHTFALL live protocol PASS")
    finally:
        device.close()


if __name__ == "__main__":
    main()
