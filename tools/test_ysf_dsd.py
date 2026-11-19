#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay the current firmware's V/D-1 and DN symbols into DSD-FME."""

import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/data/dmr_aes256_capture.txt"
SILENCE = "F801A99F8CE080"
AMBE_LINE = re.compile(r"\bAMBE ([0-9A-Fa-f]{14}) err = \[(\d+)\] \[(\d+)\]")


def check_mode(decoder, engine, directory, mode, label, expected):
    symbols = directory / f"{mode}.bin"
    audio = directory / f"{mode}.wav"
    subprocess.run([engine, "--capture", mode, symbols],
                   input=bytes.fromhex("".join(expected)), check=True, timeout=30)
    result = subprocess.run([decoder, "-fy", "-i", symbols, "-o", "null", "-w", audio, "-Z"],
                            cwd=directory, stderr=subprocess.PIPE, text=True, timeout=30)
    if result.returncode or label not in result.stderr:
        raise SystemExit(f"{mode}: DSD-FME failed to decode {label}\n{result.stderr}")
    blocks = AMBE_LINE.findall(result.stderr)
    received = [value.upper() for value, _, _ in blocks if value.upper() != SILENCE]
    if received != expected or any(c0 != "0" or total != "0" for _, c0, total in blocks):
        raise SystemExit(f"{mode}: AMBE parameters or FEC errors differ from the fixture\n{result.stderr}")
    with wave.open(str(audio)) as decoded:
        samples = decoded.readframes(decoded.getnframes())
        if decoded.getframerate() != 8000 or decoded.getsampwidth() != 2:
            raise SystemExit(f"{mode}: expected 8 kHz, 16-bit PCM from DSD-FME")
        if not any(abs(sample) >= 256 for (sample,) in struct.iter_unpack("<h", samples)):
            raise SystemExit(f"{mode}: DSD-FME WAV has no audible signal")
    print(f"{mode}: {len(received)} AMBE blocks match bit-for-bit, no FEC errors, audio decoded")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dsd-fme", default="dsd-fme", help="DSD-FME executable or path")
    parser.add_argument("--tree", type=Path, default=ROOT / "build/current",
                        help="prepared tree from tools/build_current.py")
    args = parser.parse_args()
    decoder = shutil.which(args.dsd_fme)
    if decoder is None:
        parser.error("DSD-FME executable not found")
    host = args.tree.resolve() / "tests/host"
    subprocess.run(["make", "-C", host, "ysfEngineTest"], check=True)
    expected = [line.split()[1] for line in FIXTURE.read_text().splitlines()
                if re.fullmatch(r"[0-9A-F]{14} [0-9A-F]{14}", line)]
    if not expected:
        raise SystemExit("AMBE fixture is empty")
    with tempfile.TemporaryDirectory(prefix="ysf-dsd-") as temporary:
        for mode, label in (("vd1", "V/D1"), ("dn", "V/D2")):
            check_mode(Path(decoder).resolve(), host / "ysfEngineTest", Path(temporary),
                       mode, label, expected)


if __name__ == "__main__":
    main()
