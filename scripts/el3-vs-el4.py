#!/usr/bin/env python3
"""Run the EL3-versus-EL4 benchmark matrix (el3_vs_el4/).

    el3-vs-el4.py key [--el3 --toolchain ID]
        Print the matrix key, a digest of the matrix and of the instance
        generator: results with the same matrix key are comparable. With
        --el3, print the EasyLocal 3 key, which adds the EasyLocal 3 ports,
        the legacy release and the toolchain: the EasyLocal 3 baseline is
        measured once per EasyLocal 3 key, so changing any of them measures it
        again.

    el3-vs-el4.py run --framework el3|el4 --driver PATH --output DIR
                      [--toolchain ID] [--label LABEL]
        Generate the instances, run every instance x algorithm x seed of the
        matrix with DRIVER (the EasyLocal 3 or 4 executable, same command
        line), and write DIR/results.csv and DIR/metadata.json.

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
EL3_RELEASE = "v3.3.1"

FIELDS = ["framework", "problem", "instance", "algorithm", "seed",
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


def run(args) -> int:
    matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
    output = pathlib.Path(args.output)
    instances = output / "instances"
    subprocess.run([sys.executable, str(BENCH / "generate.py"), str(MATRIX), str(instances)],
                   check=True)

    rows = []
    for instance in matrix["instances"]:
        problem, name = instance["problem"], instance["name"]
        annealing = matrix["simulated_annealing"][problem]
        for algorithm in matrix["algorithms"]:
            for seed in matrix["seeds"]:
                command = [
                    args.driver,
                    "--problem", problem,
                    "--instance", str(instances / f"{name}.instance"),
                    "--initial", str(instances / f"{name}.seed{seed}.sol"),
                    "--algorithm", algorithm,
                    "--seed", str(seed),
                ]
                if algorithm == "sa":
                    command += [
                        "--sa-start-temperature", str(annealing["start_temperature"]),
                        "--sa-min-temperature", str(annealing["min_temperature"]),
                        "--sa-cooling-rate", str(annealing["cooling_rate"]),
                        "--sa-samples", str(annealing["samples"]),
                    ]
                line = subprocess.run(command, check=True, capture_output=True,
                                      text=True).stdout.strip().splitlines()[-1]
                values = line.split(",")
                rows.append([args.framework, problem, name, algorithm, seed, *values])
                print(f"{args.framework} {name} {algorithm} seed={seed}: {line}", flush=True)

    with open(output / "results.csv", "w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(FIELDS)
        writer.writerows(rows)

    metadata = {
        "framework": args.framework,
        "label": args.label,
        "matrix_key": matrix_key(),
        "el3_key": el3_key(args.toolchain) if args.framework == "el3" else None,
        "toolchain": args.toolchain,
        "el3_release": EL3_RELEASE if args.framework == "el3" else None,
        "commit": os.environ.get("GITHUB_SHA", ""),
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "platform": f"{platform.system()} {platform.machine()}",
        "runner_image": os.environ.get("ImageOS", "") + " " + os.environ.get("ImageVersion", ""),
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)

    key = commands.add_parser("key")
    key.add_argument("--el3", action="store_true")
    key.add_argument("--toolchain", default="local")

    run_parser = commands.add_parser("run")
    run_parser.add_argument("--framework", choices=["el3", "el4"], required=True)
    run_parser.add_argument("--driver", required=True)
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
