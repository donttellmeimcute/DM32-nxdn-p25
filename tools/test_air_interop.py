#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check call identities and voice frames in simulated DM-32 TX using DSD-FME."""

import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
AMBE = r"\bAMBE [0-9A-Fa-f]{14} err = \[0\] \[0\]"
IMBE = r"\bIMBE [0-9A-Fa-f]{22} err = \[0\] \[0\]"
CAPTURE_TESTS = ("radioCoreTestAirInterop", "radioCoreTestM17", "radioCoreTestDstar")
MODES = {
    "p25": ("-f1", (r"NAC/CC: 293;", r"Group 123 Source 4567"), IMBE),
    "nxdn48": ("-fi", (r"NXDN48\b.*RAN 01", r"Src=4567.*Dst/TG=123"), AMBE),
    "nxdn96": ("-fn", (r"NXDN96\b.*RAN 01", r"Src=4567.*Dst/TG=123"), AMBE),
    "vd1": ("-fy", (r"V/D1.*\bHC\b.*DST: \*{10} +SRC: XE1ABC",), AMBE),
    "dn": ("-fy", (r"V/D2.*\bHC\b.*DST: \*{10} +SRC: XE1ABC",), AMBE),
    "vw": ("-fy", (r"VWFR.*\bHC\b.*DST: \*{10} +SRC: XE1ABC",), IMBE),
    "m17": ("-fz", (r"CAN: 5 DST: M17-M17 C SRC: XE1ABC Voice \(3200bps\)",
                    r"\(CRC CHK\) E: ([0-9A-F]{4}); C: \1;"),
            r"^ STREAM: (?:\[[0-9A-F]{2}\]){18}$"),
    "dstar": ("-fd", (r"DSTAR HEADER +RPT 2: DIRECT +RPT 1: DIRECT +DST: CQCQCQ +SRC: XE1ABC",), AMBE),
}


def dstar_discriminator(capture):
    # Feed held GMSK discriminator levels at 48 kHz. DSD-FME's .bin replay
    # expects decoded bits for D-STAR, while the board capture holds dibits.
    levels = {1: struct.pack("<h", 12000) * 10, 3: struct.pack("<h", -12000) * 10}
    symbols = capture.read_bytes()
    if set(symbols) != levels.keys():
        raise SystemExit("D-STAR capture must contain both binary GMSK levels")
    audio = capture.with_suffix(".wav")
    with wave.open(str(audio), "wb") as stream:
        stream.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        stream.writeframes(b"".join(levels[symbol] for symbol in symbols))
    return audio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=ROOT / "build/current")
    parser.add_argument("--dsd-fme", default="dsd-fme")
    args = parser.parse_args()
    decoder = shutil.which(args.dsd_fme)
    if decoder is None:
        parser.error("DSD-FME executable not found")
    host = args.tree.resolve() / "tests/host"
    subprocess.run(["make", "-C", host, *CAPTURE_TESTS], check=True)
    with tempfile.TemporaryDirectory(prefix="dm32-interop-") as temporary:
        directory = Path(temporary)
        for test in CAPTURE_TESTS:
            subprocess.run([host / test, directory], cwd=host, check=True, timeout=90)
        for mode, (decoder_flag, headers, voice) in MODES.items():
            capture = directory / f"{mode}.bin"
            if mode == "dstar":
                capture = dstar_discriminator(capture)
            result = subprocess.run(
                [str(Path(decoder).resolve()), decoder_flag, "-i", capture, "-o", "null", "-Z"],
                cwd=directory, capture_output=True, text=True, errors="replace", check=True, timeout=30,
            )
            transcript = ANSI.sub("", result.stdout + result.stderr)
            for expected in headers:
                if not re.search(expected, transcript):
                    raise SystemExit(f"{mode}: missing {expected}\n{transcript}")
            frames = len(re.findall(voice, transcript, re.MULTILINE))
            if frames < 10:
                raise SystemExit(f"{mode}: only {frames} voice frames decoded\n{transcript}")
            print(f"{mode}: DSD-FME decoded the configured call identity and {frames} voice frames")


if __name__ == "__main__":
    main()
