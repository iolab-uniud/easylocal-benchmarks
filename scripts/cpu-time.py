#!/usr/bin/env python3
"""How the CPU time of a virtual machine was spent while a benchmark ran.

    cpu-time.py start STATE           # remember the CPU counters (/proc/stat)
    cpu-time.py stop STATE METADATA   # append the shares since start

The shares go into METADATA, a key,value CSV of machine-metadata.py:
cpu_steal_percent (time the hypervisor gave to other machines on the same
host while this one wanted to run: how much the neighbours disturbed the
run), cpu_busy_percent (user and system time) and cpu_idle_percent, over all
logical CPUs. On a system without /proc/stat they are "unavailable".

Standard library only.
"""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

FIELDS = ["user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal"]


def counters() -> dict[str, int] | None:
    stat = Path("/proc/stat")
    if not stat.exists():
        return None
    for line in stat.read_text(encoding="utf-8").splitlines():
        if line.startswith("cpu "):
            values = [int(value) for value in line.split()[1:len(FIELDS) + 1]]
            return dict(zip(FIELDS, values))
    return None


def shares(before: dict[str, int], after: dict[str, int]) -> dict[str, float]:
    delta = {field: after[field] - before[field] for field in FIELDS}
    total = sum(delta.values())
    if total <= 0:
        return {}
    busy = delta["user"] + delta["nice"] + delta["system"] + delta["irq"] + delta["softirq"]
    return {
        "cpu_steal_percent": 100.0 * delta["steal"] / total,
        "cpu_busy_percent": 100.0 * busy / total,
        "cpu_idle_percent": 100.0 * (delta["idle"] + delta["iowait"]) / total,
    }


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[0] == "start":
        Path(argv[1]).write_text(json.dumps(counters()), encoding="utf-8")
        return 0
    if len(argv) >= 3 and argv[0] == "stop":
        before = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
        after = counters()
        values = shares(before, after) if before and after else {}
        rows = [
            (key, f"{values[key]:.2f}" if key in values else "unavailable")
            for key in ("cpu_steal_percent", "cpu_busy_percent", "cpu_idle_percent")
        ]
        with open(argv[2], "a", newline="", encoding="utf-8") as handle:
            csv.writer(handle, lineterminator="\n").writerows(rows)
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
