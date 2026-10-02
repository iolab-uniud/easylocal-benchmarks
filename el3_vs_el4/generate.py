#!/usr/bin/env python3
"""Generate the instances and initial solutions of the EL3-versus-EL4 matrix.

    generate.py MATRIX OUTPUT_DIR

For every instance of the matrix it writes OUTPUT_DIR/<name>.instance, in the
format of the corresponding example (examples/tsp, examples/assignment,
examples/exam_timetabling), and one initial solution per seed,
OUTPUT_DIR/<name>.seed<k>.sol: whitespace-separated integers (a tour, the
machine of each job, the timeslot of each exam). Both frameworks start from
these files, so they search from the same solution.

The output depends only on the matrix: random.Random is seeded explicitly and
its integer generation is stable across Python versions.

Standard library only.
"""

import json
import math
import pathlib
import random
import sys


def tsp(spec, rng):
    cities = spec["cities"]
    span = spec["coordinate_range"]
    points = [(rng.randrange(span), rng.randrange(span)) for _ in range(cities)]
    # Rounded Euclidean distances (TSPLIB EUC_2D): integral values, so tour
    # lengths are exact in double precision whatever the summation order.
    rows = []
    for x1, y1 in points:
        rows.append(" ".join(
            str(int(math.floor(math.hypot(x1 - x2, y1 - y2) + 0.5)))
            for x2, y2 in points))
    text = f"{cities}\n" + "\n".join(rows) + "\n"

    def initial(seed):
        tour = list(range(cities))
        random.Random(seed).shuffle(tour)
        return tour

    return text, initial


def assignment(spec, rng):
    jobs, machines = spec["jobs"], spec["machines"]
    demand = [rng.randint(1, spec["max_demand"]) for _ in range(jobs)]
    capacity = math.ceil(sum(demand) * spec["capacity_slack"] / machines)
    text = (f"{jobs} {machines}\n" + " ".join(map(str, demand)) + "\n" +
            " ".join([str(capacity)] * machines) + "\n")

    def initial(seed):
        r = random.Random(seed)
        return [r.randrange(machines) for _ in range(jobs)]

    return text, initial


def exam(spec, rng):
    exams, timeslots = spec["exams"], spec["timeslots"]
    pairs = set()
    while len(pairs) < spec["conflicts"]:
        first, second = rng.sample(range(exams), 2)
        pairs.add((min(first, second), max(first, second)))
    conflicts = [(a, b, rng.randint(1, spec["max_students"])) for a, b in sorted(pairs)]
    text = (f"{exams} {timeslots} {len(conflicts)}\n" +
            "\n".join(f"{a} {b} {s}" for a, b, s in conflicts) + "\n")

    def initial(seed):
        r = random.Random(seed)
        return [r.randrange(timeslots) for _ in range(exams)]

    return text, initial


GENERATORS = {"tsp": tsp, "assignment": assignment, "exam": exam}


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    matrix = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    output = pathlib.Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    for instance in matrix["instances"]:
        text, initial = GENERATORS[instance["problem"]](
            instance["generator"], random.Random(instance["seed"]))
        (output / f"{instance['name']}.instance").write_text(text, encoding="utf-8")
        for seed in matrix["seeds"]:
            values = initial(seed)
            (output / f"{instance['name']}.seed{seed}.sol").write_text(
                " ".join(map(str, values)) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
