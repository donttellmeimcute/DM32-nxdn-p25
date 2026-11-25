#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lyra XE1POC
"""Compile and package the DM-32UV digital mode combinations not covered by the three presets."""

import argparse
import itertools
import json
import os
from pathlib import Path
import subprocess

MODES = ("p25", "nxdn", "ysf", "m17", "dstar")
PREBUILT = {frozenset(MODES), frozenset(MODES[:3]), frozenset()}


def mode_variants():
    for count in range(1, len(MODES)):
        for selected in itertools.combinations(MODES, count):
            modes = frozenset(selected)
            if modes in PREBUILT:
                continue
            if "dstar" in modes and "p25" not in modes:
                continue
            if "m17" in modes and modes.isdisjoint(MODES[:3]):
                continue
            yield "modes-" + "-".join(selected), modes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True, help="prepared firmware tree with the D-STAR patch")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--git", required=True)
    parser.add_argument("--cross", required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    tree = args.tree.resolve()
    output = args.out.resolve()
    commit_time = subprocess.check_output(["git", "show", "-s", "--format=%ct", args.git], cwd=tree, text=True).strip()
    for variant, modes in mode_variants():
        config = ["# SPDX-License-Identifier: GPL-3.0-or-later", "# Copyright (c) 2026 lyra XE1POC"]
        config.extend(f"CONFIG_{mode.upper()} ?= {'y' if mode in modes else 'n'}" for mode in MODES)
        config.extend(("CONFIG_NET_HTTP ?= n", "CONFIG_SFR ?= n", "CONFIG_MDC1200 ?= y"))
        (tree / "config" / "variants" / f"{variant}.mk").write_text("\n".join(config) + "\n", encoding="utf-8")
        subprocess.run([
            "make", f"-j{args.jobs}", "BOARD=dm32", f"VARIANT={variant}", f"CROSS={args.cross}",
            "RFOUNDRY_PRESET=custom", f"GITVERSION={args.git[:8]}", "GITDIRTY=",
            f"RFOUNDRY_GIT_TIME={commit_time}", "all", "stack-check",
        ], cwd=tree, check=True)
        env = dict(os.environ, MAKEFLAGS=f"CROSS={args.cross}")
        subprocess.run([
            "python3", "tools/release.py", "--board", "dm32", "--variant", variant,
            "--version", args.version, "--git", args.git, "--out", str(output),
        ], cwd=tree, env=env, check=True)
        manifest_path = output / f"rfoundry-dm32-{variant}-{args.version}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        enabled = set(manifest["features"].split(","))
        actual = {mode for mode in MODES if mode.upper() in enabled}
        if actual != modes or not {"DMR", "FM", "APRS", "MDC1200", "RSSI_GRAPH"} <= enabled:
            raise SystemExit(f"{variant}: manifest does not match the requested mode configuration")


if __name__ == "__main__":
    main()
