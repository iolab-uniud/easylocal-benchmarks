#!/usr/bin/env python3
"""Run the EL3-versus-EL4 benchmark matrix (el3_vs_el4/).

    el3-vs-el4.py key [--el3 --toolchain ID]
        Print the matrix key, a digest of the matrix and of the instance
        generator: results with the same matrix key are comparable. With
        --el3, print the EasyLocal 3 key, which adds the EasyLocal 3 ports,
        the legacy release and the toolchain; it is recorded with the
        EasyLocal 3 results.

    el3-vs-el4.py run [--el3 PATH] [--el4 PATH] --output DIR
                      [--repetitions N] [--toolchain ID] [--label LABEL]
        Generate the instances and run every instance x algorithm x delta
        mode x seed of the matrix with the given drivers (same command line),
        N times each.
        The frameworks alternate run by run, in an order swapped at every
        repetition, so that a slower phase of a shared machine hits both
        alike. Writes DIR/<framework>/results.csv and metadata.json.

Standard library only.
"""

import argparse
import csv
import datetime
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BENCH = ROOT / "el3_vs_el4"
MATRIX = BENCH / "matrix.json"
EL3_RELEASE = "v3.4.1"

FIELDS = ["framework", "problem", "instance", "algorithm", "delta_mode", "seed", "repetition",
          "initial_cost", "final_cost", "evaluations", "iterations", "seconds"]


def digest_of(header: str, paths) -> str:
    digest = hashlib.sha256(header.encode())
    for path in paths:
        digest.update(str(path.relative_to(ROOT)).encode() + b"\0")
        digest.update(path.read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def matrix_key() -> str:
    return digest_of("matrix\n", [MATRIX, BENCH / "generate.py"])


def el3_key(toolchain: str) -> str:
    ports = sorted(p for p in (BENCH / "el3").rglob("*") if p.is_file())
    return digest_of(
        f"{matrix_key()}\neasylocal-legacy {EL3_RELEASE}\ntoolchain {toolchain}\n",
        ports)


def command_for(driver, matrix, instances, instance, algorithm, delta_mode, seed):
    problem, name = instance["problem"], instance["name"]
    command = [
        driver,
        "--problem", problem,
        "--instance", str(instances / f"{name}.instance"),
        "--initial", str(instances / f"{name}.seed{seed}.sol"),
        "--algorithm", algorithm,
        "--delta-mode", delta_mode,
        "--seed", str(seed),
    ]
    if algorithm == "sa":
        annealing = matrix["simulated_annealing"][problem]
        command += [
            "--sa-start-temperature", str(annealing["start_temperature"]),
            "--sa-min-temperature", str(annealing["min_temperature"]),
            "--sa-cooling-rate", str(annealing["cooling_rate"]),
            "--sa-samples", str(annealing["samples"]),
        ]
    return command


def run(args) -> int:
    matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
    output = pathlib.Path(args.output)
    instances = output / "instances"
    subprocess.run([sys.executable, str(BENCH / "generate.py"), str(MATRIX), str(instances)],
                   check=True)
    drivers = {name: path for name, path in (("el3", args.el3), ("el4", args.el4)) if path}
    if not drivers:
        raise SystemExit("give --el3 and/or --el4")

    rows = {framework: [] for framework in drivers}
    runs = [(instance, algorithm, delta_mode, seed)
            for instance in matrix["instances"]
            for algorithm in matrix["algorithms"]
            for delta_mode in matrix["delta_modes"][instance["problem"]]
            for seed in matrix["seeds"]]
    for instance, algorithm, delta_mode, seed in runs:
        for repetition in range(1, args.repetitions + 1):
            order = list(drivers) if repetition % 2 else list(reversed(drivers))
            for framework in order:
                command = command_for(drivers[framework], matrix, instances,
                                      instance, algorithm, delta_mode, seed)
                line = subprocess.run(command, check=True, capture_output=True,
                                      text=True).stdout.strip().splitlines()[-1]
                rows[framework].append([framework, instance["problem"], instance["name"],
                                        algorithm, delta_mode, seed, repetition,
                                        *line.split(",")])
                print(f"{framework} {instance['name']} {algorithm} deltas={delta_mode} "
                      f"seed={seed} #{repetition}: {line}", flush=True)

    for framework, framework_rows in rows.items():
        directory = output / framework
        directory.mkdir(parents=True, exist_ok=True)
        with open(directory / "results.csv", "w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(FIELDS)
            writer.writerows(framework_rows)
        metadata = {
            "framework": framework,
            "label": args.label,
            "matrix_key": matrix_key(),
            "el3_key": el3_key(args.toolchain) if framework == "el3" else None,
            "toolchain": args.toolchain,
            "el3_release": EL3_RELEASE if framework == "el3" else None,
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

    key = commands.add_parser("key")
    key.add_argument("--el3", action="store_true")
    key.add_argument("--toolchain", default="local")

    run_parser = commands.add_parser("run")
    run_parser.add_argument("--el3", help="the EasyLocal 3 driver")
    run_parser.add_argument("--el4", help="the EasyLocal 4 driver")
    run_parser.add_argument("--repetitions", type=int, default=3)
    run_parser.add_argument("--output", required=True)
    run_parser.add_argument("--toolchain", default="local")
    run_parser.add_argument("--label", default="local")

    args = parser.parse_args()
    if args.command == "key":
        print(el3_key(args.toolchain) if args.el3 else matrix_key())
        return 0
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
