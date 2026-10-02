# EasyLocal 3 versus EasyLocal 4

The three example problems written in both frameworks and searched with the
same algorithms from the same initial solutions. The results are rendered on the
[Benchmarks](https://iolab-uniud.github.io/easylocal/benchmarks/) page of the
EasyLocal documentation.

| File | Role |
| --- | --- |
| `matrix.json` | the benchmark matrix: instances, algorithms, annealing schedules, seeds |
| `generate.py` | writes the instances and the initial solutions of the matrix |
| `el4/` | EasyLocal 4 driver (`el4_comparison`), built on the examples of an EasyLocal checkout |
| `el3/` | EasyLocal 3 driver (`el3_comparison`), built against `easylocal-legacy` v3.3.1 |

Both drivers take the same command line,

```text
--problem tsp|assignment|exam --instance FILE --initial FILE
--algorithm sd|fd|sa --seed N
[--sa-start-temperature T --sa-min-temperature T --sa-cooling-rate R --sa-samples N]
```

and print one line, `initial_cost,final_cost,evaluations,iterations,seconds`,
where `seconds` times the search only.

## What is compared

- **Problems.** TSP with 2-opt moves, Assignment with job reassignment, Exam
  Timetabling with exam moves: the examples' cost components, delta
  evaluations and neighborhood orders, ported one to one to EasyLocal 3.
- **Costs.** The same function in both: Assignment is `1000 · total overload +
  load imbalance` (EasyLocal 3 weighs hard costs by `HARD_WEIGHT = 1000`), Exam
  Timetabling `1000 · conflicts + 10 · consecutive exams + timeslot load`.
- **Algorithms.** Steepest descent (EL3 `SteepestDescent`, EL4
  `BestImprovement`) and first descent (EL3 `FirstDescent`, EL4
  `FirstImprovement`) until a local optimum; simulated annealing with the same
  geometric schedule (EL3 `SimulatedAnnealing`, EL4 `SimulatedAnnealing<Classic>`).
  Each framework runs its own stock algorithm.
- **Known differences.** EL3 `FirstDescent` scans cyclically from the last
  move, EL4 `FirstImprovement` restarts from the first one; EL3
  `SteepestDescent` breaks ties at random, EL4 `BestImprovement` keeps the
  first best move; simulated annealing uses each framework's random numbers.
  The speed-up is therefore measured per evaluation. See `el3/README.md` for
  the details.

## Running it

From the root of this repository, with an EasyLocal checkout in `easylocal/`:

```sh
cmake -S el3_vs_el4/el4 -B build/el4 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DEASYLOCAL_SOURCE_DIR=$PWD/easylocal
cmake --build build/el4
python3 scripts/el3-vs-el4.py run --framework el4 --driver build/el4/el4_comparison \
    --output build/el4-results
```

and for EasyLocal 3 (Boost.program_options required):

```sh
curl -fsSL https://github.com/iolab-uniud/easylocal-legacy/archive/refs/tags/v3.3.1.tar.gz | tar -xz -C build
cmake -S el3_vs_el4/el3 -B build/el3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DEL3_SOURCE_DIR=$PWD/build/easylocal-legacy-3.3.1
cmake --build build/el3
python3 scripts/el3-vs-el4.py run --framework el3 --driver build/el3/el3_comparison \
    --output build/el3-results
```

In CI both frameworks are measured in the same job, on the same machine, at
every EasyLocal release: shared runners differ by up to a factor of two, so a
speed-up is only computed between results measured together.
