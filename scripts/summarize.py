#!/usr/bin/env python3
"""Write the Benchmarks page (Markdown) from the stored results.

    summarize.py [RESULTS_DIR] --output FILE

RESULTS_DIR (default: results/ of this repository) is filled by the
Benchmarks workflow, one directory per measured EasyLocal version:

    <version>/el3/{results.csv,metadata.json}   EasyLocal 3, same job
    <version>/el4/{results.csv,metadata.json}   EasyLocal 4
    <version>/infrastructure/{search,traversal,trace}.csv

The two frameworks of a version are measured on the same machine, so each
version is compared with its own EasyLocal 3 measurement.

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
DELTA_MODES = {
    "all": "every cost component has a delta evaluation",
    "mixed": "some cost components have a delta evaluation, the others are "
             "evaluated on a copy of the solution with the move applied",
    "none": "no delta evaluation: every move is applied to a copy of the "
            "solution and its cost computed from scratch",
}

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
  solutions. Each is measured with delta evaluations for all its cost
  components, for some of them, and for none (TSP has a single component, so
  no mixed mode): the trajectories are the same, only the speed changes. Both
  frameworks are measured at every release, in the same job on the same
  machine.
- **Infrastructure.** Neighborhood traversal, runner-level search and tracing
  overhead (`infrastructure/` of easylocal-benchmarks).

The speed-up compares the times per evaluation, because the two frameworks
may explore different trajectories: EasyLocal 3 first descent scans
cyclically from the last move, while EasyLocal 4 restarts from the first;
EasyLocal 3 steepest descent breaks ties at random; simulated annealing uses
each framework's random numbers. Costs are means over the seeds.
"""

DISCLAIMER = """\
!!! warning "Measured on shared virtual machines"
    The benchmarks run as a GitHub Actions workflow on a GitHub-hosted runner:
    a virtual machine on shared cloud hardware, whose speed depends on the
    load of the other machines on the same host, on the CPU model the runner
    gets and on its frequency at the time. The same run repeated can differ by
    several percent, and occasionally by more. EasyLocal 3 and EasyLocal 4 are
    measured in the same job, alternating run by run, so their ratios are
    more reliable than the absolute times; compare absolute times between
    versions only with care, and read the results as indicative, not as
    measurements on dedicated hardware.
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
    """Per (instance, algorithm, delta mode): the median time of each seed over
    its repetitions, then means over the seeds."""
    runs = defaultdict(list)
    for row in rows:
        runs[(row["instance"], row["algorithm"], row.get("delta_mode", ""),
              row["seed"])].append(row)
    groups = defaultdict(list)
    for (instance, algorithm, delta_mode, _), repetitions in runs.items():
        groups[(instance, algorithm, delta_mode)].append({
            "cost": float(repetitions[0]["final_cost"]),
            "evaluations": int(repetitions[0]["evaluations"]),
            "seconds": statistics.median(float(r["seconds"]) for r in repetitions),
        })
    result = {}
    for key, seeds in groups.items():
        seconds = sum(s["seconds"] for s in seeds)
        evaluations = sum(s["evaluations"] for s in seeds)
        result[key] = {
            "cost": statistics.fmean(s["cost"] for s in seeds),
            "seconds": seconds / len(seeds),
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


def ratio(value: float) -> str:
    """A speed-up, in bold when it favors EasyLocal 4."""
    if math.isnan(value):
        return "–"
    text = f"{value:.2f}×"
    return f"**{text}**" if value > 1 else text


def mode_order(mode: str):
    return (list(DELTA_MODES).index(mode) if mode in DELTA_MODES else len(DELTA_MODES), mode)


def comparison_table(el3, el4) -> list[str]:
    """One table per delta mode."""
    lines = []
    for mode in sorted({key[2] for key in el4}, key=mode_order):
        if lines:
            lines.append("")
        if mode:
            lines += [f"### Delta mode `{mode}`", "",
                      f"{DELTA_MODES.get(mode, mode).capitalize()}.", ""]
        lines += table_rows(el3, {k: v for k, v in el4.items() if k[2] == mode})
    return lines


def table_rows(el3, el4) -> list[str]:
    lines = [
        "| Instance | Algorithm | Speed-up | Cost EL3 | Cost EL4 | Time EL3 (s) "
        "| Time EL4 (s) | ns/eval EL3 | ns/eval EL4 |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for key in sorted(el4, key=lambda k: (k[0], list(ALGORITHMS).index(k[1]))):
        new, old = el4[key], el3.get(key)
        if old is None:
            continue
        lines.append(
            f"| {key[0]} | {ALGORITHMS.get(key[1], key[1])} "
            f"| {ratio(old['ns_per_evaluation'] / new['ns_per_evaluation'])} "
            f"| {number(old['cost'])} | {number(new['cost'])} "
            f"| {seconds(old['seconds'])} | {seconds(new['seconds'])} "
            f"| {old['ns_per_evaluation']:.1f} | {new['ns_per_evaluation']:.1f} |")
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


def variant_table(medians, row_headers, unit) -> list[str]:
    """One row per leading key, one column per variant (the last key), the
    fastest in bold."""
    variants = sorted({key[-1] for key in medians})
    rows = sorted({key[:-1] for key in medians})
    lines = ["| " + " | ".join(row_headers) + " | "
             + " | ".join(f"{v} ({unit})" for v in variants) + " |",
             "| " + " | ".join("---" for _ in row_headers) + " | "
             + " | ".join("---:" for _ in variants) + " |"]
    for row in rows:
        values = [medians.get((*row, variant), math.nan) for variant in variants]
        fastest = min((v for v in values if not math.isnan(v)), default=math.nan)
        cells = ["–" if math.isnan(v) else (f"**{v:.2f}**" if v == fastest else f"{v:.2f}")
                 for v in values]
        lines.append("| " + " | ".join(row) + " | " + " | ".join(cells) + " |")
    return lines


def infrastructure(directory: pathlib.Path) -> list[str]:
    lines = []
    search = directory / "search.csv"
    if search.exists():
        medians = median_by(read_rows(search), ["domain", "algorithm", "variant"],
                            "ns_per_evaluation")
        lines += ["", "### Runner-level search", "",
                  "Median ns per evaluation over the trials, per variant; the fastest "
                  "in bold.", "",
                  *variant_table(medians, ["Domain", "Algorithm"], "ns/eval")]
    traversal = directory / "traversal.csv"
    if traversal.exists():
        medians = median_by(read_rows(traversal), ["domain", "workload", "variant"],
                            "ns_per_move")
        lines += ["", "### Neighborhood traversal", "",
                  "Median ns per move over the trials, per variant; the fastest in "
                  "bold.", "",
                  *variant_table(medians, ["Domain", "Workload"], "ns/move")]
    trace = directory / "trace.csv"
    if trace.exists():
        medians = median_by(read_rows(trace), ["mode"], "ns_per_evaluation")
        baseline = medians.get(("baseline",), math.nan)
        lines += ["", "### Tracing overhead", "",
                  "Median ns per evaluation of a First Improvement run, without a "
                  "tracer and with each tracer; the slow-down is their ratio.", "",
                  "| Tracer | ns/eval without | ns/eval with | Slow-down |",
                  "| --- | ---: | ---: | ---: |"]
        lines += [f"| {mode} | {baseline:.2f} | {value:.2f} | {value / baseline:.2f}× |"
                  for (mode,), value in medians.items() if mode != "baseline"]
    return lines


def read_key_values(path: pathlib.Path) -> dict[str, str]:
    """The key,value rows of an infrastructure metadata.csv."""
    if not path.exists():
        return {}
    return {row["key"]: row["value"] for row in read_rows(path)}


def environment(label: str, metadata: dict, directory: pathlib.Path) -> list[str]:
    """How a version was measured: the machine, the toolchain and the method,
    from the metadata the runs recorded."""
    machine = read_key_values(directory / "infrastructure" / "metadata.csv")
    rows = [("Where", "GitHub Actions, GitHub-hosted runner"
             + (f" (image {metadata['runner_image']})" if metadata.get("runner_image") else ""))]
    if machine.get("os_description"):
        system = machine["os_description"]
        if machine.get("os_release"):
            system += f", kernel {machine['os_release']}"
        rows.append(("Operating system", system))
    elif metadata.get("platform"):
        rows.append(("Operating system", metadata["platform"]))
    if machine.get("architecture"):
        cpu = machine["architecture"]
        if machine.get("hardware_model"):
            cpu += f", {machine['hardware_model']}"
        if machine.get("logical_cpus"):
            cpu += f", {machine['logical_cpus']} logical CPUs"
        rows.append(("Architecture", cpu))
    if machine.get("compiler_version"):
        compiler = machine["compiler_version"]
        if machine.get("stdlib"):
            compiler += f"; {machine['stdlib']}"
        rows.append(("Compiler", compiler))
    elif metadata.get("toolchain"):
        rows.append(("Compiler", metadata["toolchain"]))
    build = [machine.get(key) for key in ("cmake", "generator")]
    build_type = machine.get("build_type", "Release")
    standard = machine.get("cpp_standard", "23")
    rows.append(("Build", ", ".join([b for b in build if b] + [build_type, f"C++{standard}"])))
    versions = f"EasyLocal 4 {label}"
    if machine.get("git_commit"):
        versions += f" ({machine['git_commit'][:7]})"
    if metadata.get("el3_release"):
        versions += f"; EasyLocal 3 {metadata['el3_release']}"
    elif metadata.get("framework") == "el4":
        versions += "; EasyLocal 3 v3.3.1"
    rows.append(("Versions", versions))
    if metadata.get("commit"):
        rows.append(("Benchmarks", f"easylocal-benchmarks {metadata['commit'][:7]}"))
    rows.append(("Date", metadata["date"][:10]))

    method = (
        f"Each instance, algorithm and delta mode is run on every seed of the matrix, "
        f"{metadata.get('repetitions', 3)} times per framework; a time is the median of "
        "the repetitions, then the mean over the seeds. The two frameworks run in the "
        "same job, alternating run by run, from the same initial solutions.")
    if machine.get("trials"):
        method += (
            f" The infrastructure benchmarks report the median of {machine['trials']} "
            "trials.")
    return [
        "",
        "## How it was measured",
        "",
        "| | |",
        "| --- | --- |",
        *[f"| {name} | {value} |" for name, value in rows],
        "",
        method,
        "",
        DISCLAIMER.rstrip(),
    ]


def render(results: pathlib.Path) -> str:
    current = el3_vs_el4.matrix_key()

    def measured(path: pathlib.Path):
        metadata_path = path / "metadata.json"
        if not metadata_path.exists():
            return None
        metadata = read_json(metadata_path)
        if metadata.get("matrix_key") != current:
            return None
        return metadata, aggregate(read_rows(path / "results.csv"))

    versions = []
    for directory in results.glob("*/"):
        el4 = measured(directory / "el4")
        if el4 is not None:
            versions.append((directory.name, el4, measured(directory / "el3")))
    # Releases by version number; labels that are not versions (main-<commit>)
    # all rank as 0, so among them the latest measurement wins.
    versions.sort(key=lambda v: (version_key(v[0]), v[1][0]["date"]))

    lines = [INTRO.rstrip()]
    if not versions:
        return "\n".join(lines) + "\n" + NO_RESULTS

    label, (metadata, latest), el3 = versions[-1]
    lines += environment(label, metadata, results / label)
    lines += ["", f"## EasyLocal 3 versus EasyLocal 4 {label}", ""]
    if el3 is None:
        lines += ["EasyLocal 3 was not measured with this version."]
    else:
        lines += [
            f"Measured on {metadata['date'][:10]}, both frameworks on the same "
            "machine, alternating run by run. Times are medians over the "
            "repetitions, then means over the seeds; speed-up is the ratio of "
            "the times per evaluation (higher is better for EasyLocal 4).",
            "",
            *comparison_table(el3[1], latest),
        ]
    paired = [(l, m, r, e) for l, (m, r), e in versions if e is not None]
    if len(paired) > 1:
        instances = sorted({key[0] for key in latest})
        lines += ["", "### Across versions", "",
                  "Geometric mean of the speed-ups over the algorithms and the delta "
                  "modes, per instance.", "",
                  "| Version | Date | " + " | ".join(instances) + " | Overall |",
                  "| --- | --- | " + " | ".join("---:" for _ in instances) + " | ---: |"]
        for label_i, metadata_i, results_i, el3_i in reversed(paired):
            ratios = speedups(el3_i[1], results_i)
            per_instance = [geomean(v for k, v in ratios.items() if k[0] == i)
                            for i in instances]
            lines.append(
                f"| {label_i} | {metadata_i['date'][:10]} | " +
                " | ".join(ratio(r) for r in per_instance) +
                f" | {ratio(geomean(ratios.values()))} |")

    infrastructure_dir = results / label / "infrastructure"
    infra = infrastructure(infrastructure_dir) if infrastructure_dir.exists() else []
    if infra:
        lines += ["", f"## Infrastructure of EasyLocal 4 {label}", *infra]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("results", type=pathlib.Path, nargs="?", default=ROOT / "results")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.write_text(render(args.results), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
