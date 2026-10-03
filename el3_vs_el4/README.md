# EasyLocal 3 versus EasyLocal 4

The three example problems written in both frameworks and searched with the
same algorithms from the same initial solutions. The results are rendered on the
[Benchmarks](https://iolab-uniud.github.io/easylocal/benchmarks/) page of the
EasyLocal documentation.

| File | Role |
| --- | --- |
| `matrix.json` | the benchmark matrix: problems, instances, algorithms, delta modes, annealing schedules, seeds |
| `generate.py` | writes the instances and the initial solutions of the matrix |
| `el4/` | EasyLocal 4 driver (`el4_comparison`), built on the examples of an EasyLocal checkout and on `../common/` |
| `el3/` | EasyLocal 3 driver (`el3_comparison`), built against `easylocal-legacy` v3.3.1 |

Both drivers take the same command line,

```text
--problem tsp|assignment|exam --instance FILE --initial FILE
--algorithm sd|fd|sa --seed N [--delta-mode all|mixed|none]
[--sa-start-temperature T --sa-min-temperature T --sa-cooling-rate R --sa-samples N]
```

and print one line, `initial_cost,final_cost,evaluations,iterations,seconds`,
where `seconds` times the search only. Without `--delta-mode` each problem
runs the configuration of its EasyLocal 4 example (see below).
`scripts/el3-vs-el4.py` always passes it and writes it in the `delta_mode`
column of `results.csv`.

## What is compared

- **Problems.** The examples of the current EasyLocal 4 (`examples/tsp`,
  `examples/assignment`, `examples/exam_timetabling`), used as they are by the
  EasyLocal 4 driver, and their cost components, delta evaluations and
  neighborhood orders ported one to one to EasyLocal 3:
  - *TSP*: tour length with the closing edge; 2-opt moves, enumerated by the
    first and then the second edge.
  - *Assignment*: total overload and load imbalance; job reassignment moves,
    by job and then destination machine.
  - *Exam Timetabling*: student conflicts, consecutive exams and timeslot
    load; exam moves, by exam and then destination timeslot.
- **Delta modes.** Each problem is measured with delta evaluations for all
  its cost components (`all`), for some of them (`mixed`) and for none
  (`none`, every move applied to a copy of the solution and its cost
  computed from scratch), configured alike in the two frameworks:

  | Problem | `all` | `mixed` | `none` |
  | --- | --- | --- | --- |
  | TSP | O(1) 2-opt delta (example) | – | no delta |
  | Assignment | overload and load-imbalance deltas, O(jobs) each | overload delta only | no delta (example) |
  | Exam Timetabling | conflict and consecutive-exam deltas (scan the conflicts), timeslot-load delta (recounts the loads, O(exams)) | no timeslot-load delta (example) | no delta |

  "(example)" marks the configuration of the EasyLocal 4 example, the default
  of both drivers. TSP has a single cost component, so it has no `mixed`
  mode. The examples have no whole-solution deltas (EasyLocal commit
  21bc016): the deltas they lack (assignment overload and load imbalance,
  exam timeslot load) are in `../common/` for EasyLocal 4 and in `el3/` for
  EasyLocal 3, with the same logic. The delta mode changes the speed only,
  not the trajectory: on the same seed, the three modes give the same final
  cost, evaluations and iterations in each framework.

  `matrix.json` records the problems (`problems`) and their delta modes
  (`delta_modes`); both are part of the matrix key, so results measured on
  other definitions are not compared with the current ones.
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
  The speed-up is therefore measured per evaluation. For a component without
  a delta EasyLocal 3 computes the cost of the copy and of the current
  solution at every move, while EasyLocal 4 reuses the cached value of the
  current solution. See `el3/README.md` for the details.

## Running it

From the root of this repository, with an EasyLocal checkout in `easylocal/`
(Boost.program_options is needed for EasyLocal 3):

```sh
cmake -S el3_vs_el4/el4 -B build/el4 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DEASYLOCAL_SOURCE_DIR=$PWD/easylocal
cmake --build build/el4
curl -fsSL https://github.com/iolab-uniud/easylocal-legacy/archive/refs/tags/v3.3.1.tar.gz | tar -xz -C build
cmake -S el3_vs_el4/el3 -B build/el3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DEL3_SOURCE_DIR=$PWD/build/easylocal-legacy-3.3.1
cmake --build build/el3
python3 scripts/el3-vs-el4.py run --el3 build/el3/el3_comparison \
    --el4 build/el4/el4_comparison --output build/results
```

The two drivers alternate run by run, in an order swapped at every
repetition (`--repetitions`, default 3); times are medians over the
repetitions.

In CI both frameworks are measured in the same job, on the same machine, at
every EasyLocal release: shared runners differ by up to a factor of two, so a
speed-up is only computed between results measured together.
