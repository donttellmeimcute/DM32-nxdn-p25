#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay a recorded D-STAR or YSF RF capture through pinned MMDVM receivers."""

import argparse
from collections import Counter
from math import gcd
from pathlib import Path
import re
import subprocess
import tempfile

import numpy as np
from scipy import signal

from test_mmdvm_interop import REFERENCES, ROOT, build_receivers, reference


DISCRIMINATOR_RATE = 24000


def read_filter(io_source, name):
    assignment = re.search(r"\b" + name + r"\[\]\s*=\s*\{([^}]+)\}", io_source.read_text())
    if assignment is None:
        raise SystemExit(f"MMDVM-IQ coefficient table missing: {name}")
    return np.array([int(value) for value in re.findall(r"-?\d+", assignment.group(1))]) / 32768.0


def discriminator(path, cutoff, invert, fir):
    with np.load(path) as capture:
        iq = capture["iq"]
        rate = int(capture["rate"])
        offset = int(capture["offset"])
    if iq.ndim != 1 or not np.iscomplexobj(iq) or not np.isfinite(iq).all() or len(iq) < rate:
        raise SystemExit("Expected at least a second of finite complex I/Q")
    if abs(offset) + cutoff >= rate / 2 or not 0 < cutoff < DISCRIMINATOR_RATE / 2:
        raise SystemExit("The requested channel must fit inside the capture and discriminator bandwidth")
    mixed = iq * np.exp(-2j * np.pi * offset * np.arange(iq.size) / rate)
    common = gcd(rate, DISCRIMINATOR_RATE)
    mixed = signal.resample_poly(mixed, DISCRIMINATOR_RATE // common, rate // common)
    channel = signal.sosfilt(signal.butter(6, cutoff, fs=DISCRIMINATOR_RATE, output="sos"), mixed)
    phase = np.angle(channel[1:] * np.conj(channel[:-1])) * (4096.0 / np.pi)
    power = np.abs(channel[1:]) ** 2
    block = DISCRIMINATOR_RATE // 50
    levels = power[: len(power) // block * block].reshape(-1, block).mean(axis=1)
    carrier = np.repeat(levels > np.percentile(levels, 90) / 10, block)
    if not carrier.any():
        raise SystemExit("No carrier available to estimate the discriminator offset")
    phase -= np.mean(phase[:len(carrier)][carrier])
    filtered = signal.lfilter(fir, (1,), phase * (-1 if invert else 1))
    return np.clip(np.rint(filtered), -32768, 32767).astype("<i2")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("dstar", "ysf"))
    parser.add_argument("capture", type=Path, help="NPZ with iq, rate and offset fields from the Pluto IQ recorder")
    parser.add_argument("--references", type=Path, default=ROOT / "research/protocol")
    parser.add_argument("--invert", action="store_true", help="invert discriminator polarity to match the modem")
    parser.add_argument("--cutoff", type=int, default=7200, help="RF channel filter half-width, in Hz")
    args = parser.parse_args()
    sources = [reference(args.references.resolve() / name, *source) for name, source in REFERENCES.items()]
    filter_name = "GAUSSIAN_0_5_FILTER" if args.mode == "dstar" else "RRC_0_2_FILTER"
    fir = read_filter(sources[0] / "IO.cpp", filter_name)
    pcm = discriminator(args.capture, args.cutoff, args.invert, fir)
    with tempfile.TemporaryDirectory(prefix="dm32-mmdvm-iq-") as output:
        directory = Path(output)
        build_receivers(directory, *sources)
        samples = directory / "discriminator.pcm"
        pcm.tofile(samples)
        mode = "dstar" if args.mode == "dstar" else "dn"
        received = subprocess.check_output(
            [directory / "mmdvm-iq", mode, samples, "--pcm16"], text=True, timeout=30
        )
        frames = [line.split(maxsplit=1) for line in received.splitlines()]
        counts = Counter(frame[0] for frame in frames)
        if args.mode == "dstar":
            if counts["dstar-header"] == 0 or counts["dstar-voice"] == 0:
                raise SystemExit(f"No D-STAR call recovered: {dict(counts)}")
            print(f"D-STAR: {dict(counts)}")
        else:
            payloads = directory / "ysf.bin"
            payloads.write_bytes(b"".join(bytes.fromhex(data)[1:121] for kind, data in frames if kind == "ysf"))
            subprocess.run([directory / "mmdvm-host", "ysf-rf", payloads], check=True, timeout=30)


if __name__ == "__main__":
    main()
