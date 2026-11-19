#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Decode simulated DM-32 PTT captures with DSD-FME and check their call identities."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
MODES = {
    "p25": ("-f1", r"NAC/CC: 293;", r"Group 123 Source 4567"),
    "nxdn48": ("-fi", r"NXDN48\b.*RAN 01", r"Src=4567.*Dst/TG=123"),
    "nxdn96": ("-fn", r"NXDN96\b.*RAN 01", r"Src=4567.*Dst/TG=123"),
    "dn": ("-fy", r"V/D2.*\bHC\b.*DST: ALL +SRC: XE1ABC", r"\bAMBE [0-9A-Fa-f]{14} err"),
    "vw": ("-fy", r"VWFR.*\bHC\b.*DST: ALL +SRC: XE1ABC", r"\bIMBE [0-9A-Fa-f]{22} err"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=ROOT / "build/current")
    parser.add_argument("--dsd-fme", default="dsd-fme")
    args = parser.parse_args()
    decoder = shutil.which(args.dsd_fme)
    if decoder is None:
        parser.error("DSD-FME executable not found")
    host = args.tree.resolve() / "tests/host"
    subprocess.run(["make", "-C", host, "radioCoreTestAirInterop"], check=True)
    with tempfile.TemporaryDirectory(prefix="dm32-interop-") as directory:
        subprocess.run([host / "radioCoreTestAirInterop", directory], check=True, timeout=90)
        for mode, (decoder_flag, first, second) in MODES.items():
            result = subprocess.run(
                [str(Path(decoder).resolve()), decoder_flag, "-i", f"{mode}.bin", "-o", "null", "-Z"],
                cwd=directory, capture_output=True, text=True, check=True, timeout=30,
            )
            transcript = ANSI.sub("", result.stdout + result.stderr)
            for expected in (first, second):
                if not re.search(expected, transcript):
                    raise SystemExit(f"{mode}: missing {expected}\n{transcript}")
            print(f"{mode}: DSD-FME decoded the configured call identity")


if __name__ == "__main__":
    main()
