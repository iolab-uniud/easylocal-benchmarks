# EasyLocal infrastructure benchmarks

Performance regression benchmarks of the EasyLocal infrastructure:
neighborhood traversal, runner-level search and tracing overhead. A standalone
CMake project compiled from an EasyLocal checkout (`EASYLOCAL_SOURCE_DIR`,
default `easylocal/` in this repository), whose examples provide the models.
The examples bind no delta to the assignment capacity component; the search
and tracing benchmarks bind the one in `../common/assignment_deltas.hpp`
(the delta the example had until EasyLocal commit 21bc016), so that their
assignment workload stays a light, allocation-free evaluation per move.

It is intentionally not a correctness test suite and does not impose automatic
performance thresholds. Each executable checks semantic equivalence before
reporting timings; the normal test suite remains authoritative for correctness.

The benchmark keeps the implementations that are architecturally relevant:

- `raw-cursor`: direct `first_move` / `next_move` of the benchmark's cursor
  explorers, used only as a performance oracle in the search benchmark;
- `cursor` (`cursor-range` in the search benchmark): the same cursor explorers
  through the public `easylocal::moves` adapter;
- `coroutine-custom`: a coroutine-backed input range of the benchmark
  (`generator.hpp`);
- `coroutine-std`: `std::generator`, when the active standard library provides
  it;
- `coroutine-range` (search benchmark): the example's explorer, whose
  `moves()` is an `easylocal::generator` coroutine.

Since EasyLocal 4.0.0-alpha.1 the Assignment and TSP examples enumerate their
moves with an `easylocal::generator` and no longer have a cursor: the cursor
explorers are the benchmark's own (`CursorNeighborhoodExplorer` in
`assignment_variants.hpp` and `tsp_variants.hpp`), the examples' cursors as
they were until then, with the same moves in the same order. The traversal
benchmark checks that every variant, and the example's explorer, produce the
same moves; the search benchmark that every variant reaches the same result.

Historical source-shape diagnostics and experimental workaround variants are
deliberately not retained here.

`benchmarks.json` describes, for each results file (`search`, `traversal`,
`trace`, `trace-cost-encoding`), the setup of the benchmark and every domain,
workload and variant of its table: `scripts/summarize.py` renders it as the
legend of the Benchmarks page, and warns about a name in the results that it
does not describe. A benchmark, workload or variant added to the code is
described there.

Run the complete benchmark locally, from the root of this repository, with:

```bash
EASYLOCAL_SOURCE_DIR=/path/to/easylocal \
./scripts/run-neighborhood-benchmarks.sh \
    build/neighborhood-benchmark-results \
    5000000 5 123456789
```

Performance values are diagnostic. Compare ratios within the same machine and
toolchain; do not compare absolute timings across heterogeneous CI runners.

## Search tracing overhead

The project also builds two tracing probes:

- `easylocal_trace_benchmark`, an end-to-end First Improvement benchmark that
  compares the uninstrumented baseline, explicit `null_tracer`, counter-only
  tracing, in-memory tracing, block-buffered ELTR recording, asynchronous ELTR
  recording, and buffered/asynchronous temporary-file output; on a variant of
  the problem with a solution hash it also measures buffered ELTR recording
  with the visited solutions (`binary-buffered-discard-visited`, a hash per
  committed move) and without them, left out at compile time with
  `trace::without<event::solution_visited>`
  (`binary-buffered-discard-without-visits`, which should cost what
  `binary-buffered-discard` costs);
- `easylocal_trace_cost_encoding_benchmark`, a focused ELTR encoding benchmark
  for scalar, lexicographic, and hierarchical cost payloads.

JSONL is deliberately excluded from the performance comparison: text formatting
mostly measures serialization policy rather than the tracing boundary itself.
JSONL remains a supported trace format and is covered by correctness tests.

The discard sinks remove filesystem variability while retaining binary encoding
and buffering work. File modes intentionally do not request `fsync`, so they
measure application-visible persistence overhead rather than physical-media
durability. All end-to-end modes execute the same search workload and must
produce the same checksum. That checksum consumes the final cost, termination,
evaluation count, and solution contents so the optimizer cannot discard
semantically relevant search work differently for different tracer types.

Run both probes with a Release build:

```bash
cmake -S infrastructure -B build/trace-benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DEASYLOCAL_SOURCE_DIR=/path/to/easylocal
cmake --build build/trace-benchmark --target \
  easylocal_trace_benchmark \
  easylocal_trace_cost_encoding_benchmark
./build/trace-benchmark/easylocal_trace_benchmark
./build/trace-benchmark/easylocal_trace_cost_encoding_benchmark
```

The manual **Trace Microbenchmarks** workflow of this repository runs ten process-level
end-to-end trials on Linux/GCC and macOS/AppleClang, runs the cost-encoding probe,
adds both CSV outputs to the job summary, and uploads the raw measurements as CI
artifacts. Performance values remain diagnostic: compare distributions and ratios
within one machine/toolchain rather than absolute timings across heterogeneous
runners.
