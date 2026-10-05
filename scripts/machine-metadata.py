#!/usr/bin/env python3
"""Write the machine and toolchain of a benchmark job as key,value CSV.

    machine-metadata.py --output FILE --repo-root EASYLOCAL_CHECKOUT
                        [--target-work N --trials N --seed N] [--machine-only]

Every part of the Benchmarks workflow runs in a job of its own, possibly on a
different machine, and records it with this script; the parameters of the
neighborhood benchmarks are written when given. --machine-only leaves out the
compiler (a job that builds with several toolchains records each with the
driver's own --describe).
"""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import urllib.request
from typing import Optional


def command_output(command: list[str], *, stdin: Optional[str] = None) -> str:
    try:
        completed = subprocess.run(
            command,
            input=stdin,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError:
        return "unavailable"
    if completed.returncode != 0:
        return "unavailable"
    return completed.stdout.strip()


def first_line(text: str) -> str:
    return text.splitlines()[0] if text else "unavailable"


def resolve_compiler() -> str:
    requested = os.environ.get("CXX", "c++")
    if os.path.sep in requested:
        return str(Path(requested).expanduser().resolve())
    return shutil.which(requested) or requested


def stdlib_metadata(compiler: str, cxxflags: list[str]) -> tuple[str, str]:
    macros = command_output(
        [compiler, *cxxflags, "-std=c++23", "-dM", "-E", "-x", "c++", "-"],
        stdin="#include <version>\n",
    )
    for line in macros.splitlines():
        if line.startswith("#define _LIBCPP_VERSION "):
            return "libc++", line.rsplit(" ", 1)[-1]
        if line.startswith("#define __GLIBCXX__ "):
            return "libstdc++", line.rsplit(" ", 1)[-1]
    return "unknown", "unknown"


def os_description() -> str:
    if platform.system() == "Darwin":
        name = command_output(["sw_vers", "-productName"])
        version = command_output(["sw_vers", "-productVersion"])
        build = command_output(["sw_vers", "-buildVersion"])
        return f"{name} {version} ({build})"
    os_release = Path("/etc/os-release")
    if os_release.exists():
        for line in os_release.read_text(encoding="utf-8").splitlines():
            if line.startswith("PRETTY_NAME="):
                return line.split("=", 1)[1].strip().strip('"')
    return platform.platform()


def powershell(expression: str) -> str:
    return command_output(["powershell", "-NoProfile", "-Command", expression])


def hardware_model() -> str:
    if platform.system() == "Darwin":
        cpu = command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
        model = command_output(["sysctl", "-n", "hw.model"])
        return f"{cpu} ({model})" if cpu != "unavailable" else model
    if platform.system() == "Linux":
        output = command_output(["lscpu"])
        for line in output.splitlines():
            if line.startswith("Model name:"):
                return line.split(":", 1)[1].strip()
    if platform.system() == "Windows":
        return first_line(powershell("(Get-CimInstance Win32_Processor).Name"))
    return "unknown"


# Instruction sets that change the code a compiler may emit or how fast it runs.
ISA_FLAGS = ["sse4_2", "avx", "avx2", "fma", "bmi2", "avx512f", "avx512bw", "avx512vl",
             "avx512_vnni", "sha_ni", "aes"]


def lscpu_fields() -> dict[str, str]:
    """The fields of lscpu (Linux), by their name without the colon."""
    fields = {}
    for line in command_output(["lscpu"]).splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            fields[key.strip()] = value.strip()
    return fields


def virtual_machine() -> list[tuple[str, str]]:
    """What the guest sees of its virtual machine: hypervisor, caches,
    instruction sets, frequency and memory."""
    rows = []
    if platform.system() == "Linux":
        cpu = lscpu_fields()
        for key, field in (
            ("hypervisor", "Hypervisor vendor"),
            ("virtualization", "Virtualization type"),
            ("sockets", "Socket(s)"),
            ("threads_per_core", "Thread(s) per core"),
            ("l1d_cache", "L1d cache"),
            ("l1i_cache", "L1i cache"),
            ("l2_cache", "L2 cache"),
            ("l3_cache", "L3 cache"),
            ("cpu_max_mhz", "CPU max MHz"),
        ):
            if cpu.get(field):
                rows.append((key, cpu[field]))
        mhz = cpu.get("CPU MHz")
        if not mhz and Path("/proc/cpuinfo").exists():
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
                if line.startswith("cpu MHz"):
                    mhz = line.split(":", 1)[1].strip()
                    break
        if mhz:
            rows.append(("cpu_mhz", mhz))
        flags = set(cpu.get("Flags", "").split())
        rows.append(("isa", " ".join(flag for flag in ISA_FLAGS if flag in flags)))
        meminfo = Path("/proc/meminfo")
        if meminfo.exists():
            for line in meminfo.read_text(encoding="utf-8").splitlines():
                if line.startswith("MemTotal:"):
                    kib = int(line.split()[1])
                    rows.append(("memory", f"{kib / 1024 / 1024:.1f} GiB"))
                    break
    elif platform.system() == "Darwin":
        memory = command_output(["sysctl", "-n", "hw.memsize"])
        if memory.isdigit():
            rows.append(("memory", f"{int(memory) / 1024**3:.1f} GiB"))
        for key, name in (("l1d_cache", "hw.l1dcachesize"), ("l2_cache", "hw.l2cachesize")):
            value = command_output(["sysctl", "-n", name])
            if value.isdigit():
                rows.append((key, f"{int(value) // 1024} KiB"))
    elif platform.system() == "Windows":
        memory = powershell("(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory")
        if memory.isdigit():
            rows.append(("memory", f"{int(memory) / 1024**3:.1f} GiB"))
        model = powershell("(Get-CimInstance Win32_ComputerSystem).Model")
        if model not in ("", "unavailable"):
            rows.append(("hypervisor", model))
    rows.append(("vm_size", azure_vm_size()))
    return rows


def azure_vm_size() -> str:
    """The Azure size of the virtual machine, when its instance metadata
    service answers (it may not on GitHub-hosted runners)."""
    request = urllib.request.Request(
        "http://169.254.169.254/metadata/instance/compute/vmSize"
        "?api-version=2021-02-01&format=text",
        headers={"Metadata": "true"},
    )
    try:
        with urllib.request.urlopen(request, timeout=2) as response:
            return response.read().decode("utf-8").strip() or "unavailable"
    except (OSError, ValueError):
        return "unavailable"


def git_metadata(repo_root: Path) -> tuple[str, str]:
    commit = command_output(["git", "-C", str(repo_root), "rev-parse", "HEAD"])
    status = command_output(["git", "-C", str(repo_root), "status", "--porcelain"])
    dirty = "unknown" if status == "unavailable" else ("true" if status else "false")
    return commit, dirty


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--target-work")
    parser.add_argument("--trials")
    parser.add_argument("--seed")
    parser.add_argument("--machine-only", action="store_true")
    args = parser.parse_args()

    git_commit, git_dirty = git_metadata(args.repo_root)
    cmake_version = first_line(command_output(["cmake", "--version"]))
    ninja_version = first_line(command_output(["ninja", "--version"]))

    if args.machine_only:
        compiler_rows = []
    else:
        compiler = resolve_compiler()
        cxxflags_text = os.environ.get("CXXFLAGS", "")
        cxxflags = shlex.split(cxxflags_text)
        stdlib, stdlib_version = stdlib_metadata(compiler, cxxflags)
        compiler_rows = [
            ("compiler", compiler),
            ("compiler_version", first_line(command_output([compiler, "--version"]))),
            ("compiler_target", first_line(command_output([compiler, "-dumpmachine"]))),
            ("cxxflags", cxxflags_text),
            ("stdlib", stdlib),
            ("stdlib_version", stdlib_version),
        ]

    rows = [
        ("schema_version", "1"),
        ("toolchain", "several" if args.machine_only else os.environ.get("TOOLCHAIN", "local")),
        ("os", platform.system()),
        ("os_release", platform.release()),
        ("os_description", os_description()),
        ("architecture", platform.machine()),
        ("runner_os", os.environ.get("RUNNER_OS", "local")),
        ("runner_arch", os.environ.get("RUNNER_ARCH", "local")),
        ("hardware_model", hardware_model()),
        ("logical_cpus", str(os.cpu_count() or "unknown")),
        *virtual_machine(),
        *compiler_rows,
        ("cmake", cmake_version),
        ("ninja", ninja_version),
        ("sdkroot", os.environ.get("SDKROOT", "")),
        (
            "xcode",
            first_line(command_output(["xcodebuild", "-version"]))
            if platform.system() == "Darwin"
            else "n/a",
        ),
        ("generator", "Ninja"),
        ("build_type", "Release"),
        ("cpp_standard", "23"),
        ("git_commit", git_commit),
        ("git_dirty", git_dirty),
    ]
    rows += [
        (key, value)
        for key, value in (
            ("target_work", args.target_work),
            ("trials", args.trials),
            ("seed", args.seed),
        )
        if value is not None
    ]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["key", "value"])
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
