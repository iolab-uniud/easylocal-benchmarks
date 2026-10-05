#!/usr/bin/env python3
"""Run the EL3-versus-EL4 matrix with EasyLocal 4 drivers built by several
toolchains, to compare compilers on one machine (the platforms benchmark).

    platforms.py run --platform ID --driver TOOLCHAIN=PATH [--driver ...]
                     --output DIR [--repetitions N] [--delta-modes all,...]
                     [--label LABEL]
        Generate the instances and run every instance x algorithm x delta
        mode x seed of el3_vs_el4/matrix.json with every driver, N times
        each. The drivers alternate run by run, in an order rotated at every
        repetition, so that a slower phase of a shared machine hits all of
        them alike. Writes, per toolchain, DIR/<toolchain>/results.csv (as
        el3-vs-el4.py), metadata.json and toolchain.csv (the compiler,
        standard library and flags the driver reports with --describe).

Only the delta modes given are measured (default: all, the mode in which the
framework's own work weighs most; none is dominated by copying solutions).
The machine itself is recorded by machine-metadata.py --machine-only, once
per platform.

Standard library only.
"""

import argparse
import csv
import datetime
import importlib.util
import json
import os
import pathlib
import platform
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

spec = importlib.util.spec_from_file_location("el3_vs_el4", ROOT / "scripts" / "el3-vs-el4.py")
el3_vs_el4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(el3_vs_el4)


def describe(driver: str) -> list[list[str]]:
    output = subprocess.run([driver, "--describe"], check=True, capture_output=True,
                            text=True).stdout
    return [line.split(",", 1) for line in output.strip().splitlines() if "," in line]


def run(args) -> int:
    matrix = json.loads(el3_vs_el4.MATRIX.read_text(encoding="utf-8"))
    output = pathlib.Path(args.output)
    instances = output / "instances"
    subprocess.run([sys.executable, str(el3_vs_el4.BENCH / "generate.py"),
                    str(el3_vs_el4.MATRIX), str(instances)], check=True)
    drivers = {}
    for item in args.driver:
        toolchain, _, path = item.partition("=")
        if not path:
            raise SystemExit(f"--driver takes TOOLCHAIN=PATH, not {item}")
        drivers[toolchain] = path
    modes = args.delta_modes.split(",")

    rows = {toolchain: [] for toolchain in drivers}
    runs = [(instance, algorithm, delta_mode, seed)
            for instance in matrix["instances"]
            for algorithm in matrix["algorithms"]
            for delta_mode in matrix["delta_modes"][instance["problem"]]
            if delta_mode in modes
            for seed in matrix["seeds"]]
    order = list(drivers)
    for instance, algorithm, delta_mode, seed in runs:
        for repetition in range(1, args.repetitions + 1):
            # Rotated by one at each repetition: no driver always runs first.
            order = order[1:] + order[:1]
            for toolchain in order:
                command = el3_vs_el4.command_for(drivers[toolchain], matrix, instances,
                                                 instance, algorithm, delta_mode, seed)
                line = subprocess.run(command, check=True, capture_output=True,
                                      text=True).stdout.strip().splitlines()[-1]
                rows[toolchain].append(["el4", instance["problem"], instance["name"],
                                        algorithm, delta_mode, seed, repetition,
                                        *line.split(",")])
                print(f"{toolchain} {instance['name']} {algorithm} deltas={delta_mode} "
                      f"seed={seed} #{repetition}: {line}", flush=True)

    for toolchain, toolchain_rows in rows.items():
        directory = output / toolchain
        directory.mkdir(parents=True, exist_ok=True)
        with open(directory / "results.csv", "w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(el3_vs_el4.FIELDS)
            writer.writerows(toolchain_rows)
        with open(directory / "toolchain.csv", "w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(["key", "value"])
            writer.writerow(["toolchain", toolchain])
            writer.writerows(describe(drivers[toolchain]))
        metadata = {
            "framework": "el4",
            "label": args.label,
            "platform_id": args.platform,
            "toolchain": toolchain,
            "matrix_key": el3_vs_el4.matrix_key(),
            "delta_modes": modes,
            "repetitions": args.repetitions,
            "commit": os.environ.get("GITHUB_SHA", ""),
            "date": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "platform": f"{platform.system()} {platform.machine()}",
            "runner_image": os.environ.get("ImageOS", "") + " " + os.environ.get("ImageVersion", ""),
        }
        (directory / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n",
                                                 encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--platform", required=True,
                            help="the platform's name, such as linux-x86_64")
    run_parser.add_argument("--driver", action="append", required=True,
                            metavar="TOOLCHAIN=PATH", help="an EasyLocal 4 driver")
    run_parser.add_argument("--repetitions", type=int, default=3)
    run_parser.add_argument("--delta-modes", default="all")
    run_parser.add_argument("--output", required=True)
    run_parser.add_argument("--label", default="local")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
