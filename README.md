# EasyLocal benchmarks

Performance benchmarks of [EasyLocal](https://github.com/iolab-uniud/easylocal),
a C++23 framework for local search, against its predecessor EasyLocal 3
([easylocal-legacy](https://github.com/iolab-uniud/easylocal-legacy)) and
across its releases. The results are rendered on the
[Benchmarks](https://iolab-uniud.github.io/easylocal/benchmarks/) page of the
EasyLocal documentation.

| Directory | Content |
| --- | --- |
| [`el3_vs_el4/`](el3_vs_el4/README.md) | the EasyLocal example problems written in both frameworks, the benchmark matrix and its instance generator |
| [`infrastructure/`](infrastructure/README.md) | neighborhood traversal, runner-level search and tracing overhead of EasyLocal |
| `common/` | EasyLocal 4 code the examples do not provide, shared by the benchmarks |
| `scripts/` | `el3-vs-el4.py` runs the matrix, `run-neighborhood-benchmarks.sh` and `run-trace-benchmarks.sh` the infrastructure benchmarks, `machine-metadata.py` records the machine of a job, `summarize.py` writes the results page |
| `results/` | the measured results, committed by CI |

## How it runs

The Benchmarks workflow starts when EasyLocal publishes a release (the
EasyLocal CI sends a `repository_dispatch` with the release tag) or by hand:

1. **Prepare** fixes the EasyLocal commit to measure and the label of the
   results, so that every part measures the same commit.
2. Each part runs in a job of its own, one after the other, and records the
   machine it ran on:
   - **EasyLocal 3 versus EasyLocal 4**: the matrix, both frameworks in the
     same job, on the same machine (shared runners differ by up to a factor
     of two, so a speed-up is only computed between results measured
     together);
   - **Neighborhood**: traversal and runner-level search
     (`scripts/run-neighborhood-benchmarks.sh`);
   - **Trace**: tracing overhead and ELTR cost encoding
     (`scripts/run-trace-benchmarks.sh`).
3. **Publish** merges the parts into `results/<version>/`, commits them here
   and rebuilds the EasyLocal documentation, so that its Benchmarks page shows
   them. Results of different matrices (`scripts/el3-vs-el4.py key`) are not
   compared.

A manual run can measure one part only (input `parts`); publishing it
(input `publish`, false by default) replaces that part's files and keeps the
others.

All runs use a GitHub-hosted Ubuntu runner with GCC 16 in Release mode. Times
on shared runners vary between runs: compare ratios and trends. The manual
**Neighborhood Benchmarks** and **Trace Microbenchmarks** workflows measure the
infrastructure of any EasyLocal ref across toolchains (Linux GCC and Clang,
macOS AppleClang and GCC) for diagnosis; their results are artifacts only.

## Adding a problem

Write the problem in both frameworks with the same cost function, delta
evaluations and neighborhood order, give both drivers the command line of
`el3_vs_el4/README.md` (delta modes included), and add its instances and
delta modes to the matrix.

## License

MIT, as EasyLocal.
