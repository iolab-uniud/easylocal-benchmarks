# EasyLocal 3 side of the EL3-versus-EL4 benchmark

A standalone CMake project that ports the three EasyLocal 4 examples used by
`../el4/driver.cpp`, in their current form, (TSP 2-opt, assignment reassign-job, exam-timetabling
move-exam) to EasyLocal 3 (release v3.3.1) and runs them with the same command
line and the same output.

## Build

EasyLocal 3 is header-only; it needs Boost.program_options and Threads.

```sh
curl -L https://github.com/iolab-uniud/easylocal-legacy/archive/refs/tags/v3.3.1.tar.gz | tar xz
cmake -S el3_vs_el4/el3 -B build/el3 -DCMAKE_BUILD_TYPE=Release \
      -DEL3_SOURCE_DIR=$PWD/easylocal-legacy-3.3.1
cmake --build build/el3
```

The project includes EasyLocal 3's own `CMakeLists.txt` (`add_subdirectory`)
and links its `EasyLocal` INTERFACE target (C++17, `-Wall`); TBB lookup is
disabled because none of the headers used here need it. No other compiler
flags are added; the EasyLocal 3 headers only produce warnings
(`-Wdeprecated-literal-operator` in `json.hpp` with clang, `-Wcatch-value` in
`firstdescent.hh` with GCC).

## Usage

```
el3_comparison --problem tsp|assignment|exam --instance FILE --initial FILE
               --algorithm sd|fd|sa --seed N
               [--sa-start-temperature T --sa-min-temperature T
                --sa-cooling-rate R --sa-samples N]
```

It prints `initial_cost,final_cost,evaluations,iterations,seconds`: costs are
`CostStructure::total`, evaluations and iterations are the runner's
`Evaluations()` and `Iteration()`, seconds time `Runner::Go()` only. Two extra
option, not in the EasyLocal 4 driver: `--output FILE`, which writes the final
solution.

## Equivalences

- Costs. TSP: one soft component, `double` costs, tour length with the
  closing edge. Assignment: hard total overload (weight 1) plus soft load
  imbalance (weight 1), so `total = HARD_WEIGHT (1000) * overload + imbalance`,
  which is the EasyLocal 4 driver's `cost::sum(total_overload * 1000,
  load_imbalance)`; costs are `long`. Exam: three soft components with weights
  1000, 10 and 1, `long` costs.
- Deltas are ported line by line, with the same complexity: O(1) 2-opt delta;
  the student-conflict and consecutive-exam deltas scan the conflicts.
- The components without a delta in the EasyLocal 4 examples have none here
  either: the total overload and the load imbalance (assignment) and the
  timeslot load (exam). They are attached with
  `NeighborhoodExplorer::AddCostComponent`, which wraps them in a
  `DeltaCostComponentAdapter`; `DeltaCostFunctionComponents` then copies the
  solution once per move, applies the move to the copy and computes
  `ComputeCost(copy) - ComputeCost(current)` for each of them. EasyLocal 4
  also evaluates a copy, but reuses the cached value of the current solution,
  so EasyLocal 3 evaluates these components twice per move instead of once.
- `FirstMove`/`NextMove` enumerate in the order of the EasyLocal 4
  `first_move`/`next_move`; `RandomMove` draws uniformly as the EasyLocal 4
  `random_move`, with `Random::Uniform`: two edges redrawn until they form a
  valid 2-opt move (TSP), or an ordinal over the jobs or exams and their other
  machines or timeslots (assignment, exam). An empty neighborhood throws `EmptyNeighborhood`.
  `FeasibleMove` is the EasyLocal 4 `is_valid`.
- Algorithms: `sd` is `SteepestDescent`; `fd` is `FirstDescent`; `sa` is `SimulatedAnnealing` with
  `start_temperature`, `min_temperature`, `cooling_rate` and
  `neighbors_sampled` set (no computed start temperature, no accepted-ratio
  cut-off, unlimited evaluations).

## Differences

- `fd`: EasyLocal 3 `FirstDescent` scans cyclically, from the move after the
  last one applied (`SelectFirst(start_move, ...)`), while EasyLocal 4
  `FirstImprovement` restarts every scan from the first move: the two follow
  different trajectories. In v3.3 that cyclic scan never terminated; v3.3.1
  fixes it (and is why the benchmark uses v3.3.1). When the last move applied
  is no longer in the neighborhood, as in assignment and exam, the final scan
  explores some moves twice.
- `sd`: `SelectBest` breaks ties among the best moves at random, EasyLocal 4
  `BestImprovement` keeps the first one, so the trajectories diverge at the
  first tie.
- `sa`: the random streams differ (EasyLocal 3 uses a global `std::mt19937`,
  and `std::uniform_int_distribution` differs between libc++ and libstdc++);
  the schedule and the number of evaluations are the same. An EasyLocal 3
  iteration is one accepted move (or the end of a temperature), not one
  sampled move.
- Counters: EasyLocal 3 counts the final iteration that finds no improving
  move, and does not count the evaluation of the initial solution, so on the
  same trajectory it reports one iteration more and one evaluation less than
  EasyLocal 4.
