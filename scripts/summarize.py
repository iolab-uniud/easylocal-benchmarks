#!/usr/bin/env python3
"""Write the Benchmarks page (Markdown) from the stored results.

    summarize.py [RESULTS_DIR] --output FILE [--toolchain gcc16]

RESULTS_DIR (default: results/ of this repository) is filled by the
Benchmarks workflow:

    el3/<el3 key>/{results.csv,metadata.json}         EasyLocal 3, once per key
    el4/<version>/comparison/{results.csv,metadata.json}
    el4/<version>/infrastructure/{search,traversal,trace}.csv

The EasyLocal documentation renders its Benchmarks page with this script.

Only results measured on the current matrix (scripts/el3-vs-el4.py key) are
compared. A missing or empty RESULTS_DIR gives the page with no results.

Standard library only.
"""

import argparse
import csv
import importlib.util
import json
import math
import pathlib
import statistics
import sys
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent

spec = importlib.util.spec_from_file_location("el3_vs_el4", ROOT / "scripts" / "el3-vs-el4.py")
el3_vs_el4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(el3_vs_el4)

ALGORITHMS = {"sd": "steepest descent", "fd": "first descent", "sa": "simulated annealing"}

INTRO = """\
# Benchmarks

EasyLocal 4 is measured at every release against EasyLocal 3 (the
[v3.3.1 tag](https://github.com/iolab-uniud/easylocal-legacy/tree/v3.3.1)
of `easylocal-legacy`) and on its own infrastructure. The numbers are produced
in [easylocal-benchmarks](https://github.com/iolab-uniud/easylocal-benchmarks)
on a GitHub-hosted runner (Ubuntu, GCC 16, Release); each EasyLocal release
starts a run.

- **EasyLocal 3 versus EasyLocal 4.** The three example problems (TSP with
  2-opt, Assignment with job reassignment, Exam Timetabling with exam moves)
  are written in both frameworks with the same cost functions, delta
  evaluations and neighborhood orders, and searched from the same initial
  solutions. EasyLocal 3 is measured once per benchmark matrix; EasyLocal 4 at
  every release.
- **Infrastructure.** Neighborhood traversal, runner-level search and tracing
  overhead (`benchmarks/neighborhood_traversal` of EasyLocal).

Times on shared CI runners vary by several percent between runs and machines:
read ratios and trends, not absolute values. The speed-up compares the times
per evaluation, because the two frameworks may explore different trajectories:
EasyLocal 3 first descent scans cyclically from the last move, while
EasyLocal 4 restarts from the first; EasyLocal 3 steepest descent breaks ties
at random; simulated annealing uses each framework's random numbers. Costs
are means over the seeds.
"""

NO_RESULTS = """
No results have been published yet: they appear after the first run of the
Benchmarks workflow on a release.
"""


def read_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with open(path, newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def read_json(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def version_key(label: str):
    parts = label.lstrip("v").split(".")
    return tuple(int(p) if p.isdigit() else 0 for p in parts)


def aggregate(rows):
    """Means over the seeds, per (instance, algorithm)."""
    groups = defaultdict(list)
    for row in rows:
        groups[(row["instance"], row["algorithm"])].append(row)
    result = {}
    for key, group in groups.items():
        seconds = sum(float(r["seconds"]) for r in group)
        evaluations = sum(int(r["evaluations"]) for r in group)
        result[key] = {
            "cost": statistics.fmean(float(r["final_cost"]) for r in group),
            "seconds": seconds / len(group),
            "ns_per_evaluation": seconds * 1e9 / evaluations if evaluations else math.nan,
        }
    return result


def number(value: float) -> str:
    if math.isnan(value):
        return "–"
    if value == int(value) and abs(value) < 1e15:
        return f"{int(value):,}".replace(",", " ")
    return f"{value:,.1f}".replace(",", " ")


def seconds(value: float) -> str:
    return f"{value:.3f}"


def comparison_table(el3, el4) -> list[str]:
    lines = [
        "| Instance | Algorithm | Cost EL3 | Cost EL4 | Time EL3 (s) | Time EL4 (s) "
        "| ns/eval EL3 | ns/eval EL4 | Speed-up |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for key in sorted(el4, key=lambda k: (k[0], list(ALGORITHMS).index(k[1]))):
        new, old = el4[key], el3.get(key)
        if old is None:
            continue
        lines.append(
            f"| {key[0]} | {ALGORITHMS.get(key[1], key[1])} "
            f"| {number(old['cost'])} | {number(new['cost'])} "
            f"| {seconds(old['seconds'])} | {seconds(new['seconds'])} "
            f"| {old['ns_per_evaluation']:.1f} | {new['ns_per_evaluation']:.1f} "
            f"| {old['ns_per_evaluation'] / new['ns_per_evaluation']:.2f}× |")
    return lines


def speedups(el3, el4) -> dict:
    return {key: el3[key]["ns_per_evaluation"] / el4[key]["ns_per_evaluation"]
            for key in el4 if key in el3}


def geomean(values) -> float:
    values = [v for v in values if v > 0 and not math.isnan(v)]
    return math.exp(statistics.fmean(math.log(v) for v in values)) if values else math.nan


def median_by(rows, keys, value):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[k] for k in keys)].append(float(row[value]))
    return {key: statistics.median(values) for key, values in groups.items()}


def infrastructure(directory: pathlib.Path) -> list[str]:
    lines = []
    search = directory / "search.csv"
    if search.exists():
        medians = median_by(read_rows(search), ["domain", "algorithm", "variant"],
                            "ns_per_evaluation")
        lines += ["", "### Runner-level search", "",
                  "Median ns per evaluation over the trials.", "",
                  "| Domain | Algorithm | Variant | ns/eval |",
                  "| --- | --- | --- | ---: |"]
        lines += [f"| {d} | {a} | {v} | {value:.2f} |"
                  for (d, a, v), value in sorted(medians.items())]
    traversal = directory / "traversal.csv"
    if traversal.exists():
        medians = median_by(read_rows(traversal), ["domain", "workload", "variant"],
                            "ns_per_move")
        lines += ["", "### Neighborhood traversal", "",
                  "Median ns per move over the trials.", "",
                  "| Domain | Workload | Variant | ns/move |",
                  "| --- | --- | --- | ---: |"]
        lines += [f"| {d} | {w} | {v} | {value:.2f} |"
                  for (d, w, v), value in sorted(medians.items())]
    trace = directory / "trace.csv"
    if trace.exists():
        medians = median_by(read_rows(trace), ["mode"], "ns_per_evaluation")
        baseline = medians.get(("baseline",), math.nan)
        lines += ["", "### Tracing overhead", "",
                  "Median ns per evaluation of a First Improvement run, per tracer.", "",
                  "| Tracer | ns/eval | vs. no tracer |",
                  "| --- | ---: | ---: |"]
        lines += [f"| {mode} | {value:.2f} | {value / baseline:.2f}× |"
                  for (mode,), value in medians.items()]
    return lines


def render(results: pathlib.Path, toolchain: str) -> str:
    current = el3_vs_el4.matrix_key()
    el3 = None
    for metadata_path in sorted(results.glob("el3/*/metadata.json")):
        metadata = read_json(metadata_path)
        if metadata.get("matrix_key") == current and metadata.get("toolchain") == toolchain:
            el3 = (metadata, aggregate(read_rows(metadata_path.parent / "results.csv")))

    versions = []
    for metadata_path in results.glob("el4/*/comparison/metadata.json"):
        metadata = read_json(metadata_path)
        if metadata.get("matrix_key") == current:
            versions.append((metadata["label"], metadata,
                             aggregate(read_rows(metadata_path.parent / "results.csv"))))
    versions.sort(key=lambda v: version_key(v[0]))

    lines = [INTRO.rstrip()]
    if not versions:
        return "\n".join(lines) + "\n" + NO_RESULTS

    label, metadata, latest = versions[-1]
    lines += ["", f"## EasyLocal 3 versus EasyLocal 4 {label}", ""]
    if el3 is None:
        lines += ["The EasyLocal 3 baseline for the current matrix has not been measured yet."]
    else:
        el3_metadata, el3_results = el3
        lines += [
            f"EasyLocal 3 measured on {el3_metadata['date'][:10]}, EasyLocal 4 "
            f"{label} on {metadata['date'][:10]}. Means over "
            f"the seeds; speed-up is the ratio of the times per evaluation "
            "(higher is better for EasyLocal 4).",
            "",
            *comparison_table(el3_results, latest),
        ]
        if len(versions) > 1:
            instances = sorted({key[0] for key in latest})
            lines += ["", "### Across versions", "",
                      "Geometric mean of the speed-ups over the algorithms, per instance.", "",
                      "| Version | Date | " + " | ".join(instances) + " | Overall |",
                      "| --- | --- | " + " | ".join("---:" for _ in instances) + " | ---: |"]
            for label_i, metadata_i, results_i in reversed(versions):
                ratios = speedups(el3_results, results_i)
                per_instance = [geomean(v for k, v in ratios.items() if k[0] == i)
                                for i in instances]
                lines.append(
                    f"| {label_i} | {metadata_i['date'][:10]} | " +
                    " | ".join(f"{r:.2f}×" for r in per_instance) +
                    f" | {geomean(ratios.values()):.2f}× |")

    infrastructure_dir = results / "el4" / label / "infrastructure"
    infra = infrastructure(infrastructure_dir) if infrastructure_dir.exists() else []
    if infra:
        lines += ["", f"## Infrastructure of EasyLocal 4 {label}", *infra]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("results", type=pathlib.Path, nargs="?", default=ROOT / "results")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--toolchain", default="gcc16")
    args = parser.parse_args()
    args.output.write_text(render(args.results, args.toolchain), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
