#!/usr/bin/env python3
"""Validate generated NIGHTFALL song previews without external audio packages."""
import array
import hashlib
import math
import sys
import wave


def inspect(path):
    with wave.open(path, "rb") as source:
        assert source.getnchannels() == 2
        assert source.getsampwidth() == 2
        assert source.getframerate() == 44118
        assert source.getnframes() == 44118 * 12
        samples = array.array("h", source.readframes(source.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max(abs(sample) for sample in samples)
    rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples))
    assert 50 < peak < 32767, (path, peak)
    assert rms > 10, (path, rms)
    digest = hashlib.sha256(samples.tobytes()).hexdigest()
    print(f"{path}: peak={peak}, rms={rms:.1f}, sha256={digest[:12]}")
    return digest


if __name__ == "__main__":
    assert len(sys.argv) == 4
    hashes = [inspect(path) for path in sys.argv[1:]]
    assert len(set(hashes)) == 3, "song previews must differ"
