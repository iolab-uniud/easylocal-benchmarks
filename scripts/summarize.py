#!/usr/bin/env python3
"""Write the Benchmarks page (Markdown) from the stored results.

    summarize.py [RESULTS_DIR] --output FILE

RESULTS_DIR (default: results/ of this repository) is filled by the
Benchmarks workflow, one directory per measured EasyLocal version:

    <version>/el3/{results.csv,metadata.json}   EasyLocal 3, same job
    <version>/el4/{results.csv,metadata.json}   EasyLocal 4
    <version>/infrastructure/{search,traversal,trace}.csv
    <version>/platforms/<platform>/metadata.csv                 the machine
    <version>/platforms/<platform>/<toolchain>/{results.csv,metadata.json,toolchain.csv}

The two frameworks of a version are measured on the same machine, so each
version is compared with its own EasyLocal 3 measurement.

The EasyLocal documentation renders its Benchmarks page with this script.
The charts are Plotly figures written as JSON next to their tables; the page
loads plotly.js from its CDN when it has charts.

The legends come from the benchmarks themselves: the instances and algorithms
of the comparison from el3_vs_el4/matrix.json, the setups and variants of the
infrastructure benchmarks from infrastructure/benchmarks.json (a variant of
the results it does not describe is reported on standard error).

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
             "evaluated on a copy of the solution with the move applied; in "
             "the union of neighborhoods, the delta of the 2-opt moves only",
    "none": "no delta evaluation: every move is applied to a copy of the "
            "solution and its cost computed from scratch",
}

INTRO = """\
# Benchmarks

EasyLocal 4 is measured at every release against EasyLocal 3 (the
[v3.4.1 tag](https://github.com/iolab-uniud/easylocal-legacy/tree/v3.4.1)
of `easylocal-legacy`) and on its own infrastructure. The numbers are produced
in [easylocal-benchmarks](https://github.com/iolab-uniud/easylocal-benchmarks)
on a GitHub-hosted runner (Ubuntu, GCC 16, Release); each EasyLocal release
starts a run.

- **EasyLocal 3 versus EasyLocal 4.** The three example problems (TSP with
  2-opt, Assignment with job reassignment, Exam Timetabling with exam moves)
  are written in both frameworks with the same cost functions, delta
  evaluations and neighborhood orders, and searched from the same initial
  solutions. The TSP is searched a second time with the union of the 2-opt and
  the swap neighborhoods (EasyLocal 3 `SetUnion`, EasyLocal 4
  `neighborhood_union`), which measures the cost of a compound neighborhood in
  both frameworks. Each is measured with delta evaluations for all its cost
  components, for some of them, and for none (the TSP with 2-opt has a single
  component and a single neighborhood, so no mixed mode; the union has the
  delta of the 2-opt moves only): the trajectories are the same, only the
  speed changes. Both frameworks are measured at every release, in the same
  job on the same machine.
- **Infrastructure.** Neighborhood traversal, runner-level search and tracing
  overhead (`infrastructure/` of easylocal-benchmarks).
- **Compilers and architectures.** The EasyLocal 4 side of the comparison
  built by several toolchains, on Linux x86_64 and ARM64, macOS ARM64 and
  Windows x86_64: each platform in a job of its own, its toolchains
  alternating run by run.

The speed-up compares the times per evaluation, because the two frameworks
may explore different trajectories: EasyLocal 3 first descent starts each
scan from a random move, while EasyLocal 4 starts from the first;
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

# How each problem's generator parameters read, for the legend of the instances.
INSTANCE_DESCRIPTIONS = {
    "tsp": lambda g: (f"TSP, {g['cities']} cities at random integer coordinates in a "
                      f"{g['coordinate_range']} × {g['coordinate_range']} square "
                      "(rounded Euclidean distances)"),
    "tsp-union": lambda g: (f"TSP, {g['cities']} cities at random integer coordinates in a "
                            f"{g['coordinate_range']} × {g['coordinate_range']} square "
                            "(rounded Euclidean distances), searched with the union of the "
                            "2-opt and the swap neighborhoods"),
    "assignment": lambda g: (f"Assignment, {g['jobs']} jobs of demand 1 to "
                             f"{g['max_demand']} on {g['machines']} machines of equal "
                             f"capacity, {g['capacity_slack']:.0%} of the total demand "
                             "(so some overload cannot be avoided)"),
    "exam": lambda g: (f"Exam Timetabling, {g['exams']} exams in {g['timeslots']} "
                       f"timeslots, {g['conflicts']} pairs of conflicting exams, each "
                       f"shared by 1 to {g['max_students']} students"),
}

COLUMNS = """\
- **Speed-up**: ns/eval of EasyLocal 3 divided by ns/eval of EasyLocal 4; in
  bold when EasyLocal 4 is faster.
- **Cost**: the final cost, mean over the seeds (lower is better). The two
  frameworks may follow different trajectories (see above), so the costs may
  differ even where the speed is the same.
- **Time**: the seconds of the search alone, median over the repetitions, mean
  over the seeds. It depends on how many evaluations the trajectory takes, and
  the first descents of the two frameworks, scanning from a random move and
  from the first one, may take very different numbers of them. Compare times within a framework, and ns/eval across them.
- **ns/eval**: the time divided by the evaluations of solutions and moves.
"""

INFRASTRUCTURE_LEGEND = ROOT / "infrastructure" / "benchmarks.json"

# The platforms of the platforms benchmark, in the order of the page, with
# the toolchain the others of the same platform are compared with.
PLATFORMS = {
    "linux-x86_64": ("Linux x86_64", "gcc16"),
    "linux-arm64": ("Linux ARM64", "gcc16"),
    "macos-arm64": ("macOS ARM64", "appleclang"),
    "windows-x86_64": ("Windows x86_64", "clang-cl"),
}
TOOLCHAINS = ["gcc16", "clang23-libstdcxx", "clang23-libcxx", "appleclang", "clang-cl", "msvc"]

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


def comparison_legend(matrix) -> list[str]:
    """What the instances, algorithms and columns of the comparison are."""
    problems = {name: text.split("; delta modes")[0]
                for name, text in matrix.get("problems", {}).items()}
    instances = [f"  - `{i['name']}`: "
                 f"{INSTANCE_DESCRIPTIONS[i['problem']](i['generator'])}"
                 + (f"; cost {problems[i['problem']]}" if i["problem"] in problems else "")
                 + ";"
                 for i in matrix["instances"] if i["problem"] in INSTANCE_DESCRIPTIONS]
    if instances:
        instances[-1] = instances[-1][:-1] + "."
    schedules = [f"{problem} starts at {s['start_temperature']:g}, cools by "
                 f"{s['cooling_rate']:g} every {s['samples']} proposals and ends "
                 f"at {s['min_temperature']:g}"
                 for problem, s in matrix.get("simulated_annealing", {}).items()]
    seeds = len(matrix.get("seeds", []))
    algorithms = [f"  - {a['description']};" for a in matrix.get("algorithms", {}).values()]
    if schedules:
        algorithms.append("  - the schedule of simulated annealing, the same in both: "
                          + "; ".join(schedules) + ".")
    elif algorithms:
        algorithms[-1] = algorithms[-1][:-1] + "."
    return [
        "- **Instances**, generated by `el3_vs_el4/generate.py`; each is searched "
        f"from {seeds} random initial solutions, one per seed:",
        *instances,
        "- **Algorithms**, each framework's own:",
        *algorithms,
        *COLUMNS.rstrip().splitlines(),
    ]



# The charts are Plotly figures: summarize.py writes each one's data and
# layout as JSON in the page (standard library only), and the loader below,
# written once at the end of the page, fetches plotly.js from its CDN, draws
# them and follows the light or dark scheme of the documentation. The series
# colors are a colorblind-safe quadruple (blue, orange, aqua, yellow); every
# chart stands next to the table with the same numbers.
SERIES_COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
PLOTLY_URL = "https://cdn.plot.ly/plotly-basic-4.1.1.min.js"

CHART_LOADER = """\
<script>
(function () {
  function scheme() {
    var dark = document.body.getAttribute("data-md-color-scheme") === "slate";
    var text = getComputedStyle(document.body).color;
    var grid = dark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.12)";
    return { font: { color: text, family: "inherit" }, grid: grid };
  }
  function draw() {
    var theme = scheme();
    document.querySelectorAll(".benchmark-chart").forEach(function (element) {
      var figure = JSON.parse(element.getAttribute("data-figure"));
      var layout = figure.layout;
      layout.paper_bgcolor = "rgba(0,0,0,0)";
      layout.plot_bgcolor = "rgba(0,0,0,0)";
      layout.font = theme.font;
      ["xaxis", "yaxis"].forEach(function (axis) {
        layout[axis] = layout[axis] || {};
        layout[axis].gridcolor = theme.grid;
        layout[axis].zerolinecolor = theme.grid;
      });
      (layout.shapes || []).forEach(function (shape) {
        shape.line = shape.line || {};
        shape.line.color = theme.font.color;
      });
      Plotly.react(element, figure.data, layout,
                   { responsive: true, displaylogo: false,
                     modeBarButtonsToRemove: ["select2d", "lasso2d", "autoScale2d"] });
    });
  }
  if (!document.querySelector(".benchmark-chart")) { return; }
  var script = document.createElement("script");
  script.src = "%s";
  script.onload = function () {
    draw();
    new MutationObserver(draw).observe(
      document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] });
  };
  document.head.appendChild(script);
})();
</script>
""" % PLOTLY_URL


def chart(title: str, figure: dict, height: int) -> list[str]:
    """The HTML of a figure, under its caption: a div the loader draws into,
    with the figure as JSON."""
    encoded = json.dumps(figure, separators=(",", ":")).replace("&", "&amp;").replace(
        "'", "&#39;").replace("<", "&lt;")
    return ["", f"*{title}.*", "",
            f'<div class="benchmark-chart" style="height: {height}px" '
            f"data-figure='{encoded}'></div>", ""]


def bar_chart(title: str, rows, series, unit: str, reference: float | None = None,
              value_format=lambda v: f"{v:.2f}") -> list[str]:
    """A horizontal grouped bar chart: one group per row, one bar per series,
    the value at the tip; a vertical line at reference, when given."""
    # A label of several parts goes on as many lines, leaving the plot the width.
    labels = [label.replace(", ", "<br>") for label, _ in rows]
    data = []
    for index, name in enumerate(series):
        values = [cells.get(name, math.nan) for _, cells in rows]
        data.append({
            "type": "bar", "orientation": "h", "name": name,
            "y": labels, "x": [None if math.isnan(v) else v for v in values],
            "text": ["" if math.isnan(v) else value_format(v) for v in values],
            "textposition": "outside", "cliponaxis": False,
            "marker": {"color": SERIES_COLORS[index % len(SERIES_COLORS)]},
            "hovertemplate": "%{y}<br>" + name + ": %{text}<extra></extra>",
        })
    # Each group holds its bars, and at least its label's lines; the gap keeps
    # the bars thin where the label makes the group taller than they need.
    lines_per_label = max((label.count("<br>") + 1 for label in labels), default=1)
    group_height = max(14 * len(series), 18 * lines_per_label) + 14
    layout = {
        "barmode": "group", "bargroupgap": 0.1,
        "bargap": max(0.3, 1 - 20.0 * len(series) / group_height),
        "margin": {"l": 10, "r": 10, "t": 30, "b": 40},
        "legend": {"orientation": "h", "y": 1.0, "yanchor": "bottom", "x": 1, "xanchor": "right"},
        # Room on the right for the value written at the tip of the longest bar.
        "xaxis": {"title": {"text": unit}, "automargin": True,
                  "range": [0, 1.12 * max((v for _, cells in rows
                                           for v in cells.values() if not math.isnan(v)),
                                          default=1.0)]},
        "yaxis": {"autorange": "reversed", "automargin": True, "ticksuffix": "  "},
    }
    if reference is not None:
        layout["shapes"] = [{"type": "line", "x0": reference, "x1": reference,
                             "y0": 0, "y1": 1, "yref": "paper", "line": {"width": 1}}]
    height = 80 + len(rows) * group_height
    return chart(title, {"data": data, "layout": layout}, height)


def line_chart(title: str, labels, series, unit: str, reference: float | None = None) -> list[str]:
    """Lines over ordered labels (versions), one per series."""
    data = [{
        "type": "scatter", "mode": "lines+markers", "name": name,
        "x": labels, "y": [None if math.isnan(v) else v for v in points],
        "line": {"color": SERIES_COLORS[index % len(SERIES_COLORS)], "width": 2},
        "marker": {"size": 8},
        "hovertemplate": "%{x}<br>" + name + ": %{y:.2f}×<extra></extra>",
    } for index, (name, points) in enumerate(series.items())]
    layout = {
        "margin": {"l": 10, "r": 10, "t": 30, "b": 40},
        "legend": {"orientation": "h", "y": 1.0, "yanchor": "bottom", "x": 1, "xanchor": "right"},
        "xaxis": {"type": "category", "automargin": True},
        "yaxis": {"title": {"text": unit}, "rangemode": "tozero", "automargin": True},
    }
    if reference is not None:
        layout["shapes"] = [{"type": "line", "y0": reference, "y1": reference,
                             "x0": 0, "x1": 1, "xref": "paper", "line": {"width": 1}}]
    return chart(title, {"data": data, "layout": layout}, 320)


def speedup_chart(el3, el4) -> list[str]:
    """The speed-ups of every instance and algorithm, one bar per delta mode."""
    modes = sorted({key[2] for key in el4}, key=mode_order)
    rows = []
    for key in sorted({k[:2] for k in el4}, key=lambda k: (k[0], list(ALGORITHMS).index(k[1]))):
        cells = {mode: el3[(*key, mode)]["ns_per_evaluation"] / el4[(*key, mode)]["ns_per_evaluation"]
                 for mode in modes if (*key, mode) in el3 and (*key, mode) in el4}
        if cells:
            rows.append((f"{key[0]}, {ALGORITHMS.get(key[1], key[1])}", cells))
    return bar_chart("Speed-up of EasyLocal 4 over EasyLocal 3, by delta mode", rows,
                     modes, "speed-up (×)", reference=1.0, value_format=lambda v: f"{v:.2f}×")


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


def variant_chart(title: str, medians, unit: str) -> list[str]:
    variants = sorted({key[-1] for key in medians})
    rows = [(", ".join(row), {v: medians.get((*row, v), math.nan) for v in variants})
            for row in sorted({key[:-1] for key in medians})]
    return bar_chart(title, rows, variants, unit, value_format=lambda v: f"{v:.1f} {unit}")


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


def legend(name: str, present) -> list[str]:
    """The setup and the legend of a results file, from infrastructure/benchmarks.json;
    present lists, per legend group, the names the results contain."""
    if not INFRASTRUCTURE_LEGEND.exists():
        return []
    entry = read_json(INFRASTRUCTURE_LEGEND).get(name)
    if entry is None:
        print(f"warning: {name} is not described in {INFRASTRUCTURE_LEGEND.name}",
              file=sys.stderr)
        return []
    lines = [entry["setup"], ""]
    groups = entry.get("legend", {})
    for group, items in groups.items():
        lines.append(f"- **{group}**:")
        lines += [f"  - `{item}`: {text};" for item, text in items.items()]
        lines[-1] = lines[-1][:-1] + "."
    documented = {item for items in groups.values() for item in items}
    for item in sorted(set(present) - documented):
        print(f"warning: {name}: {item} is not described in {INFRASTRUCTURE_LEGEND.name}",
              file=sys.stderr)
    return lines + [""]


def infrastructure(directory: pathlib.Path) -> list[str]:
    lines = []
    search = directory / "search.csv"
    if search.exists():
        medians = median_by(read_rows(search), ["domain", "algorithm", "variant"],
                            "ns_per_evaluation")
        lines += ["", "### Runner-level search", "",
                  "Median ns per evaluation over the trials, per variant; the fastest "
                  "in bold.", "",
                  *legend("search", {v for key in medians for v in key}),
                  *variant_chart("Runner-level search, ns per evaluation", medians, "ns/eval"),
                  *variant_table(medians, ["Domain", "Algorithm"], "ns/eval")]
    traversal = directory / "traversal.csv"
    if traversal.exists():
        medians = median_by(read_rows(traversal), ["domain", "workload", "variant"],
                            "ns_per_move")
        lines += ["", "### Neighborhood traversal", "",
                  "Median ns per move over the trials, per variant; the fastest in "
                  "bold.", "",
                  *legend("traversal", {v for key in medians for v in key}),
                  *variant_chart("Neighborhood traversal, ns per move", medians, "ns/move"),
                  *variant_table(medians, ["Domain", "Workload"], "ns/move")]
    trace = directory / "trace.csv"
    if trace.exists():
        medians = median_by(read_rows(trace), ["mode"], "ns_per_evaluation")
        baseline = medians.get(("baseline",), math.nan)
        lines += ["", "### Tracing overhead", "",
                  "Median ns per evaluation of a First Improvement run, without a "
                  "tracer and with each tracer; the slow-down is their ratio.", "",
                  *legend("trace", {key[0] for key in medians} - {"baseline"}),
                  "| Tracer | ns/eval without | ns/eval with | Slow-down |",
                  "| --- | ---: | ---: | ---: |"]
        lines += [f"| {mode} | {baseline:.2f} | {value:.2f} | {value / baseline:.2f}× |"
                  for (mode,), value in medians.items() if mode != "baseline"]
    encoding = directory / "trace-cost-encoding.csv"
    if encoding.exists():
        rows = read_rows(encoding)
        times = median_by(rows, ["cost_model"], "ns_per_event")
        sizes = {row["cost_model"]: row["bytes_per_event"] for row in rows}
        lines += ["", "### ELTR cost encoding", "",
                  "Median ns per encoded event, by the layout of the cost.", "",
                  *legend("trace-cost-encoding", {key[0] for key in times}),
                  "| Cost | ns/event | bytes/event |",
                  "| --- | ---: | ---: |"]
        lines += [f"| {model} | {value:.2f} | {sizes[model]} |"
                  for (model,), value in times.items()]
    return lines


def read_key_values(path: pathlib.Path) -> dict[str, str]:
    """The key,value rows of an infrastructure metadata.csv."""
    if not path.exists():
        return {}
    return {row["key"]: row["value"] for row in read_rows(path)}


# The parts of a measurement and the file of the machine each ran on; results
# measured before the parts had jobs of their own have only the neighborhood
# one, which then describes the whole run.
PARTS = [
    ("EasyLocal 3 vs 4", "el3-vs-el4-metadata.csv"),
    ("Neighborhood", "infrastructure/metadata.csv"),
    ("Trace", "infrastructure/trace-metadata.csv"),
]


def machine_rows(machine: dict[str, str]) -> dict[str, str]:
    """The operating system, architecture, compiler and build of a machine."""
    rows = {}
    if machine.get("os_description"):
        system = machine["os_description"]
        if machine.get("os_release"):
            system += f", kernel {machine['os_release']}"
        rows["Operating system"] = system
    if machine.get("architecture"):
        cpu = machine["architecture"]
        if machine.get("hardware_model"):
            cpu += f", {machine['hardware_model']}"
        if machine.get("logical_cpus"):
            cpu += f", {machine['logical_cpus']} logical CPUs"
        rows["Architecture"] = cpu
    if machine.get("hypervisor"):
        virtual = machine["hypervisor"]
        if machine.get("virtualization"):
            virtual += f" ({machine['virtualization']} virtualization)"
        if machine.get("vm_size") not in (None, "", "unavailable"):
            virtual += f", size {machine['vm_size']}"
        rows["Virtual machine"] = virtual
    caches = [f"{name} {machine[key]}" for name, key in (
        ("L1d", "l1d_cache"), ("L1i", "l1i_cache"), ("L2", "l2_cache"), ("L3", "l3_cache"))
        if machine.get(key)]
    if caches:
        rows["Caches"] = ", ".join(caches)
    if machine.get("cpu_mhz"):
        rows["Frequency"] = f"{float(machine['cpu_mhz']):.0f} MHz nominal"
    if machine.get("memory"):
        rows["Memory"] = machine["memory"]
    if machine.get("isa"):
        rows["Instruction sets"] = machine["isa"].replace(" ", ", ")
    if machine.get("cpu_steal_percent") not in (None, "", "unavailable"):
        rows["CPU steal during the run"] = (
            f"{float(machine['cpu_steal_percent']):.1f}% (busy "
            f"{float(machine['cpu_busy_percent']):.0f}%)")
    if machine.get("compiler_version"):
        compiler = machine["compiler_version"]
        if machine.get("stdlib"):
            compiler += f"; {machine['stdlib']}"
        rows["Compiler"] = compiler
    build = [machine.get(key) for key in ("cmake", "generator")]
    rows["Build"] = ", ".join(
        [b for b in build if b]
        + [machine.get("build_type", "Release"), f"C++{machine.get('cpp_standard', '23')}"])
    return rows


def environment(label: str, metadata: dict, directory: pathlib.Path) -> list[str]:
    """How a version was measured: the machine of each part, the toolchain and
    the method, from the metadata the runs recorded."""
    parts = [(name, read_key_values(directory / path)) for name, path in PARTS]
    parts = [(name, machine) for name, machine in parts if machine]
    if len(parts) == 1 and parts[0][0] == "Neighborhood":
        parts = [("All parts", parts[0][1])]

    where = "GitHub Actions, GitHub-hosted runner"
    if metadata.get("runner_image"):
        where += f" (image {metadata['runner_image']})"
    commit = next(
        (machine["git_commit"][:7] for _, machine in parts if machine.get("git_commit")), "")
    versions = f"EasyLocal 4 {label}" + (f" ({commit})" if commit else "")
    versions += f"; EasyLocal 3 {metadata.get('el3_release') or 'v3.4.1'}"
    lines = [
        "",
        "## How it was measured",
        "",
        f"- **Where:** {where}.",
        f"- **Versions:** {versions}.",
    ]
    if metadata.get("commit"):
        lines.append(f"- **Benchmarks:** easylocal-benchmarks {metadata['commit'][:7]}.")
    lines.append(f"- **Date:** {metadata['date'][:10]}.")
    if len(parts) > 1:
        lines.append(
            "- **Jobs:** each part runs in a job of its own, one after the other, "
            "possibly on a different machine; EasyLocal 3 and EasyLocal 4 always "
            "share theirs.")

    if parts:
        rows = [(name, machine_rows(machine)) for name, machine in parts]
        keys = list(dict.fromkeys(key for _, row in rows for key in row))
        lines += [
            "",
            "| | " + " | ".join(name for name, _ in rows) + " |",
            "| --- | " + " | ".join("---" for _ in rows) + " |",
            *[
                f"| {key} | " + " | ".join(row.get(key, "–") for _, row in rows) + " |"
                for key in keys
            ],
        ]

    neighborhood = read_key_values(directory / "infrastructure" / "metadata.csv")
    method = (
        f"Each instance, algorithm and delta mode is run on every seed of the matrix, "
        f"{metadata.get('repetitions', 3)} times per framework; a time is the median of "
        "the repetitions, then the mean over the seeds. The two frameworks run in the "
        "same job, alternating run by run, from the same initial solutions.")
    if neighborhood.get("trials"):
        method += (
            f" The neighborhood benchmarks report the median of {neighborhood['trials']} "
            "trials")
        trace = read_key_values(directory / "infrastructure" / "trace-metadata.csv")
        method += (
            f", the tracing ones of {trace['trials']}." if trace.get("trials") else ".")
    if any(machine.get("cpu_steal_percent") not in (None, "", "unavailable")
           for _, machine in parts):
        method += (
            " CPU steal is the share of the virtual machine's CPU time that the "
            "hypervisor gave to other machines on the same host while the part ran: "
            "the higher it is, the more the neighbours disturbed the measurement.")
    return lines + ["", method, "", DISCLAIMER.rstrip()]


def platform_order(name: str):
    return (list(PLATFORMS).index(name) if name in PLATFORMS else len(PLATFORMS), name)


def toolchain_order(name: str):
    return (TOOLCHAINS.index(name) if name in TOOLCHAINS else len(TOOLCHAINS), name)


def platforms(directory: pathlib.Path, label: str) -> list[str]:
    """The compilers-and-architectures section: the machines, then one table
    per platform with the ns/eval of each toolchain."""
    current = el3_vs_el4.matrix_key()
    measured = {}
    for platform_dir in sorted(directory.glob("*/"), key=lambda p: platform_order(p.name)):
        toolchains = {}
        for toolchain_dir in platform_dir.glob("*/"):
            metadata_path = toolchain_dir / "metadata.json"
            if not metadata_path.exists():
                continue
            metadata = read_json(metadata_path)
            if metadata.get("matrix_key") != current:
                continue
            toolchains[toolchain_dir.name] = (
                metadata, aggregate(read_rows(toolchain_dir / "results.csv")),
                read_key_values(toolchain_dir / "toolchain.csv"))
        if toolchains:
            measured[platform_dir.name] = (
                read_key_values(platform_dir / "metadata.csv"),
                dict(sorted(toolchains.items(), key=lambda t: toolchain_order(t[0]))))
    if not measured:
        return []

    def title(name: str) -> str:
        return PLATFORMS.get(name, (name, None))[0]

    modes = sorted({key[2] for _, toolchains in measured.values()
                    for _, results, _ in toolchains.values() for key in results},
                   key=mode_order)
    lines = [
        "", f"## Compilers and architectures of EasyLocal 4 {label}", "",
        "The EasyLocal 4 driver of the comparison above, built by several "
        "toolchains and run on the same instances, algorithms and seeds, in delta "
        f"mode {', '.join(f'`{m}`' for m in modes)}. Each platform runs in a job of "
        "its own, on its own machine, and its toolchains alternate run by run, so "
        "the times of one platform are comparable with each other; the platforms "
        "are different (shared) machines, so their absolute times are not. The "
        "descents follow the same trajectory with every toolchain; simulated "
        "annealing may not, as the standard libraries draw random numbers "
        "differently, hence ns/eval rather than times.",
    ]
    rows = [(title(name), machine_rows(machine)) for name, (machine, _) in measured.items()]
    keys = list(dict.fromkeys(key for _, row in rows for key in row))
    lines += [
        "",
        "| | " + " | ".join(name for name, _ in rows) + " |",
        "| --- | " + " | ".join("---" for _ in rows) + " |",
        *[f"| {key} | " + " | ".join(row.get(key, "–") for _, row in rows) + " |"
          for key in keys],
    ]
    for name, (machine, toolchains) in measured.items():
        reference = PLATFORMS.get(name, (None, None))[1]
        if reference not in toolchains:
            reference = next(iter(toolchains))
        date = next(iter(toolchains.values()))[0]["date"][:10]
        lines += ["", f"### {title(name)}", "",
                  f"Measured on {date}.", "",
                  "| Toolchain | Compiler | Standard library | Flags |",
                  "| --- | --- | --- | --- |"]
        for toolchain, (_, _, described) in toolchains.items():
            compiler = described.get("compiler", "")
            if described.get("compiler_version"):
                compiler += f" {described['compiler_version']}"
            stdlib = described.get("stdlib", "")
            if described.get("stdlib_version"):
                stdlib += f" {described['stdlib_version']}"
            flags = described.get("cxxflags", "")
            lines.append(f"| `{toolchain}` | {compiler} | {stdlib} | "
                         f"{f'`{flags}`' if flags else '–'} |")
        lines += ["",
                  "| Instance | Algorithm" + (" | Deltas" if len(modes) > 1 else "")
                  + " | " + " | ".join(f"`{t}` (ns/eval)" for t in toolchains) + " |",
                  "| --- | ---" + (" | ---" if len(modes) > 1 else "")
                  + " | " + " | ".join("---:" for _ in toolchains) + " |"]
        keys = sorted({key for _, results, _ in toolchains.values() for key in results},
                      key=lambda k: (k[0], list(ALGORITHMS).index(k[1]), mode_order(k[2])))
        for key in keys:
            values = [results.get(key, {}).get("ns_per_evaluation", math.nan)
                      for _, results, _ in toolchains.values()]
            fastest = min((v for v in values if not math.isnan(v)), default=math.nan)
            cells = ["–" if math.isnan(v) else (f"**{v:.1f}**" if v == fastest else f"{v:.1f}")
                     for v in values]
            lines.append(f"| {key[0]} | {ALGORITHMS.get(key[1], key[1])}"
                         + (f" | {key[2]}" if len(modes) > 1 else "")
                         + " | " + " | ".join(cells) + " |")
        relative = []
        for toolchain, (_, results, _) in toolchains.items():
            if toolchain == reference:
                continue
            ratios = [results[k]["ns_per_evaluation"]
                      / toolchains[reference][1][k]["ns_per_evaluation"]
                      for k in results if k in toolchains[reference][1]]
            relative.append(f"`{toolchain}` {geomean(ratios):.2f}×")
        if relative:
            lines += ["", f"Time per evaluation relative to `{reference}`, geometric mean "
                      f"over the rows (below 1 is faster): {', '.join(relative)}."]
            chart_rows = []
            for key in keys:
                base = toolchains[reference][1].get(key, {}).get("ns_per_evaluation", math.nan)
                cells = {t: results[key]["ns_per_evaluation"] / base
                         for t, (_, results, _) in toolchains.items()
                         if key in results and t != reference}
                chart_rows.append((f"{key[0]}, {ALGORITHMS.get(key[1], key[1])}"
                                   + (f", {key[2]}" if len(modes) > 1 else ""), cells))
            lines += bar_chart(f"{title(name)}: time per evaluation relative to {reference}",
                               chart_rows, [t for t in toolchains if t != reference],
                               f"time per evaluation relative to {reference} (×)",
                               reference=1.0,
                               value_format=lambda v: f"{v:.2f}×")
    return lines


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
    el3_tag = (el3[0].get("el3_release") if el3 else None) or ""
    lines += ["", f"## EasyLocal 3 {el3_tag} versus EasyLocal 4 {label}".replace("  ", " "), ""]
    if el3 is None:
        lines += ["EasyLocal 3 was not measured with this version."]
    else:
        lines += [
            f"Measured on {metadata['date'][:10]}, EasyLocal 4 {label} against "
            f"EasyLocal 3 [{el3_tag}](https://github.com/iolab-uniud/easylocal-legacy/"
            f"releases/tag/{el3_tag}), both frameworks on the same "
            "machine, alternating run by run. Times are medians over the "
            "repetitions, then means over the seeds; speed-up is the ratio of "
            "the times per evaluation (higher is better for EasyLocal 4).",
            "",
            *comparison_legend(json.loads(
                el3_vs_el4.MATRIX.read_text(encoding="utf-8"))),
            *speedup_chart(el3[1], latest),
            *comparison_table(el3[1], latest),
        ]
    paired = [(l, m, r, e) for l, (m, r), e in versions if e is not None]
    if len(paired) > 1:
        instances = sorted({key[0] for key in latest})
        lines += ["", "### Across versions", "",
                  "Geometric mean of the speed-ups over the algorithms and the delta "
                  "modes, per instance.", "",
                  "| Version | EasyLocal 3 | Date | " + " | ".join(instances) + " | Overall |",
                  "| --- | --- | --- | " + " | ".join("---:" for _ in instances) + " | ---: |"]
        trend = {instance: [] for instance in instances}
        trend["Overall"] = []
        for label_i, metadata_i, results_i, el3_i in reversed(paired):
            ratios = speedups(el3_i[1], results_i)
            per_instance = [geomean(v for k, v in ratios.items() if k[0] == i)
                            for i in instances]
            lines.append(
                f"| {label_i} | {el3_i[0].get('el3_release') or '–'} "
                f"| {metadata_i['date'][:10]} | " +
                " | ".join(ratio(r) for r in per_instance) +
                f" | {ratio(geomean(ratios.values()))} |")
            for instance, value in zip(instances, per_instance):
                trend[instance].insert(0, value)
            trend["Overall"].insert(0, geomean(ratios.values()))
        lines += line_chart("Speed-up across versions, geometric mean per instance",
                            [l for l, *_ in paired], trend, "speed-up (×)", reference=1.0)

    infrastructure_dir = results / label / "infrastructure"
    infra = infrastructure(infrastructure_dir) if infrastructure_dir.exists() else []
    if infra:
        lines += ["", f"## Infrastructure of EasyLocal 4 {label}", *infra]
    platforms_dir = results / label / "platforms"
    if platforms_dir.exists():
        lines += platforms(platforms_dir, label)
    if any('class="benchmark-chart"' in line for line in lines):
        lines += ["", CHART_LOADER.rstrip()]
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
