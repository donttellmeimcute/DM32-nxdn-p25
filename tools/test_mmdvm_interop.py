#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay DM-32 TX captures through MMDVM-IQ and MMDVM-Host."""
import argparse
from collections import Counter
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests/mmdvm"
REFERENCES = {
    "MMDVM-IQ": ("https://github.com/g4klx/MMDVM-IQ.git", "eb7e5ae084e55047dba5f7191c07ddbea49230f9"),
    "MMDVM-M17": ("https://github.com/M17-Project/MMDVM.git", "e7ea81f85bc90cba8734d5ffba8f795b83beb1f7"),
    "MMDVMHost": ("https://github.com/g4klx/MMDVMHost.git", "6ffa6d354a2c185316201c27c831c766a25a5653"),
}
CAPTURE_TESTS = ("radioCoreTestAirInterop", "radioCoreTestM17", "radioCoreTestDstar")
MODES = ("dmr", "p25", "nxdn48", "nxdn96", "vd1", "dn", "vw", "m17", "dstar")
P25_SYNC = bytes.fromhex("5575f5ff77ff")
PAYLOAD_SIZE = {
    "dmr": 33,
    "p25-header": 99,
    "p25-ldu": 216,
    "nxdn": 48,
    "ysf": 120,
    "m17-link": 48,
    "m17-voice": 48,
}


def reference(directory, url, commit):
    git = ["git", "-C", directory]
    if not (directory / ".git").is_dir():
        directory.mkdir(parents=True, exist_ok=True)
        subprocess.run([*git, "init", "-q"], check=True)
        subprocess.run([*git, "fetch", "-q", "--depth", "1", url, commit], check=True)
        subprocess.run([*git, "checkout", "-q", "--detach", "FETCH_HEAD"], check=True)
    head = subprocess.check_output([*git, "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output([*git, "status", "--porcelain"], text=True)
    if head != commit or dirty:
        raise SystemExit(f"{directory}: expected a clean checkout at {commit}")
    return directory


def build_receivers(directory, iq, m17, host):
    cxx = ["c++", "-std=c++17", "-O2"]
    replay = [*cxx, "-include", HARNESS / "receiver_io.h", "-I", HARNESS, HARNESS / "replay.cc"]
    subprocess.run([
        *replay, "-I", iq, *(iq / f"{name}.cpp" for name in
                             ("DMRDMORX", "DMRSlotType", "DStarRX", "NXDNRX", "P25RX", "YSFRX", "Utils")),
        "-o", directory / "mmdvm-iq",
    ], check=True)
    subprocess.run([
        *replay, "-DTEST_M17", "-I", m17, m17 / "M17RX.cpp", m17 / "Utils.cpp",
        "-o", directory / "mmdvm-m17",
    ], check=True)
    subprocess.run([
        *cxx, "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-I", host,
        HARNESS / "inspect.cc", *(host / f"{name}.cpp" for name in
                                  ("BPTC19696", "DMRFullLC", "DMRLC", "RS129", "Hamming", "YSFFICH",
                                   "YSFConvolution", "CRC", "Golay24128", "Utils", "P25Data", "P25NID",
                                   "P25Trellis", "P25Utils", "RS634717", "BCH")),
        "-o", directory / "mmdvm-host",
    ], check=True)
    subprocess.run([
        *cxx, "-I", host, *(host / f"{name}.cpp" for name in
                            ("NXDNLICH", "NXDNSACCH", "NXDNFACCH1", "NXDNConvolution", "NXDNCRC")),
        ROOT / "tests/nxdn/stubs/utils_count.cc", ROOT / "tests/nxdn/test_nxdn_current_mmdvm.cc",
        "-o", directory / "mmdvm-host-nxdn",
    ], check=True)


def dibits(payload):
    return bytes((byte >> shift) & 3 for byte in payload for shift in (6, 4, 2, 0))


def check_frames(mode, capture, output):
    """Validate the serial frame sequence against the actual transmitted payload."""
    counts = Counter()
    matched = []
    cursor = 0
    for line in output.splitlines():
        kind, _, hex_data = line.partition(" ")
        frame = bytes.fromhex(hex_data)
        counts[kind] += 1
        if kind == "dstar-header":
            expected = b"\0\0\0DIRECT  DIRECT  CQCQCQ  XE1ABC      "
            if len(frame) < 41 or frame[:39] != expected:
                raise SystemExit("D-STAR: MMDVM decoded an incorrect DV header")
            continue
        if kind in ("dstar-end", "m17-end"):
            continue
        if kind == "dstar-voice":
            if len(frame) < 12:
                raise SystemExit("D-STAR: MMDVM returned a truncated DV frame")
            payload = frame[:12]
            symbols = bytes(1 if (byte >> bit) & 1 else 3 for byte in payload for bit in range(8))
        else:
            length = PAYLOAD_SIZE[kind]
            payload = frame[1:1 + length]
            if len(payload) != length:
                raise SystemExit(f"{mode}: truncated {kind} from MMDVM")
            if kind == "p25-ldu" and (payload[:6] != P25_SYNC or payload[7] & 0x0F not in (5, 10)):
                if counts["p25-ldu"] > 1:
                    raise SystemExit("P25: MMDVM lost LDU alignment after acquiring the call")
                del counts["p25-ldu"]
                counts["p25-acquiring"] += 1
                continue
            symbols = dibits(payload)

        # These ring-buffer receivers can forward a stale final dibit: the
        # readout is scheduled one sample before that position is overwritten.
        window = symbols if kind in ("dstar-voice", "p25-header") else symbols[:-1]
        start = capture.find(window, cursor)
        if start < 0 or start + len(symbols) > len(capture):
            raise SystemExit(f"{mode}: MMDVM {kind} {counts[kind]} differs from the TX capture")
        cursor = start + len(symbols)
        matched.append((kind, frame[:1 + len(payload)] if kind == "dmr" else payload))
    return counts, matched


def validate(mode, directory):
    capture = directory / f"{mode}.bin"
    receiver = directory / ("mmdvm-m17" if mode == "m17" else "mmdvm-iq")
    output = subprocess.check_output([receiver, mode, capture], text=True, timeout=30)
    counts, frames = check_frames(mode, capture.read_bytes(), output)
    payloads = directory / f"{mode}.received"
    if mode == "dstar":
        if counts["dstar-header"] != 1 or counts["dstar-end"] != 1 or counts["dstar-voice"] < 10:
            raise SystemExit("D-STAR: missing header, voice or end-of-transmission")
    elif mode == "m17":
        if counts["m17-link"] != 1 or counts["m17-end"] != 1 or counts["m17-voice"] < 10:
            raise SystemExit("M17: missing link setup, voice or end-of-transmission")
    elif mode.startswith("nxdn"):
        # MMDVM-IQ uses a 2400-symbol/s NXDN receive clock. Feed the NXDN96
        # frame capture at that clock to verify the protocol, including LICH
        # and FACCH. NXDN96 demodulation at its native rate is outside this test.
        payloads.write_bytes(bytes(10) + b"".join(dibits(data) for _, data in frames))
        subprocess.run([directory / "mmdvm-host-nxdn", payloads], check=True, timeout=30)
    else:
        if mode == "p25" and counts["p25-header"] < 1:
            raise SystemExit("P25: MMDVM did not decode the header")
        payloads.write_bytes(b"".join(data for kind, data in frames if kind != "p25-header"))
        subprocess.run([directory / "mmdvm-host", mode, payloads], check=True, timeout=30)
    if mode == "nxdn96":
        print(f"{mode}: {dict(counts)} at the MMDVM NXDN48 symbol clock", flush=True)
    else:
        print(f"{mode}: MMDVM receive results {dict(counts)}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=ROOT / "build/current")
    parser.add_argument("--references", type=Path, default=ROOT / "research/protocol")
    args = parser.parse_args()
    sources = [reference(args.references.resolve() / name, *source) for name, source in REFERENCES.items()]
    host = args.tree.resolve() / "tests/host"
    subprocess.run(["make", "-C", host, *CAPTURE_TESTS], check=True)
    with tempfile.TemporaryDirectory(prefix="dm32-mmdvm-") as output:
        directory = Path(output)
        build_receivers(directory, *sources)
        for test in CAPTURE_TESTS:
            subprocess.run([host / test, directory], cwd=host, check=True, timeout=90)
        for mode in MODES:
            validate(mode, directory)


if __name__ == "__main__":
    main()
