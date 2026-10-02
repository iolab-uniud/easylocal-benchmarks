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
| `scripts/` | `el3-vs-el4.py` runs the matrix, `summarize.py` writes the results page |
| `results/` | the measured results, committed by CI |

## How it runs

The Benchmarks workflow starts when EasyLocal publishes a release (the
EasyLocal CI sends a `repository_dispatch` with the release tag) or by hand:

1. **EasyLocal 3** is measured once per EasyLocal 3 key: a digest of the
   matrix, the instance generator, the EasyLocal 3 ports, the legacy release
   and the toolchain. A stored baseline in `results/el3/<key>/` is reused.
2. **EasyLocal 4** is measured at the requested release, together with the
   infrastructure benchmarks of that release, into `results/el4/<version>/`.
3. The results are committed here, and the EasyLocal documentation is rebuilt
   so that its Benchmarks page shows them.

All runs use a GitHub-hosted Ubuntu runner with GCC 16 in Release mode. Times
on shared runners vary between runs: compare ratios and trends.

## Adding a problem

Write the problem in both frameworks with the same cost function, delta
evaluations and neighborhood order, give both drivers the command line of
`el3_vs_el4/README.md`, and add its instances to the matrix.

## License

MIT, as EasyLocal.
