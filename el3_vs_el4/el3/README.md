# EasyLocal 3 side of the EL3-versus-EL4 benchmark

A standalone CMake project that ports the three EasyLocal 4 examples used by
`../el4/driver.cpp`, in their current form, (TSP 2-opt, assignment reassign-job, exam-timetabling
move-exam) to EasyLocal 3 (release v3.4.1) and runs them with the same command
line and the same output.

## Build

EasyLocal 3 is header-only; it needs Boost.program_options and Threads.

```sh
curl -L https://github.com/iolab-uniud/easylocal-legacy/archive/refs/tags/v3.4.1.tar.gz | tar xz
cmake -S el3_vs_el4/el3 -B build/el3 -DCMAKE_BUILD_TYPE=Release \
      -DEL3_SOURCE_DIR=$PWD/easylocal-legacy-3.4.1
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
               --algorithm sd|fd|sa --seed N [--delta-mode all|mixed|none]
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
  the student-conflict and consecutive-exam deltas scan the conflicts; the
  overload delta recomputes both machine loads in O(jobs), the load-imbalance
  delta all the loads in O(jobs); the timeslot-load delta recounts the loads
  in O(exams). The last three are the deltas of `../../common/`, which the
  EasyLocal 4 examples do not have.
- `--delta-mode` selects them as the EasyLocal 4 driver does (see
  `../README.md`). A component without a delta in the chosen mode is attached
  with `NeighborhoodExplorer::AddCostComponent`, which wraps it in a
  `DeltaCostComponentAdapter`; `DeltaCostFunctionComponents` then copies the
  solution once per move, applies the move to the copy and computes
  `ComputeCost(copy) - ComputeCost(current)` for each such component. EasyLocal
  4 also evaluates a copy, but reuses the cached value of the current
  solution, so EasyLocal 3 evaluates these components twice per move instead
  of once.
- `FirstMove`/`NextMove` enumerate in the order of the EasyLocal 4
  `first_move`/`next_move`; `RandomMove` draws uniformly as the EasyLocal 4
  `random_move`, with `Random::Uniform`: two edges redrawn until they form a
  valid 2-opt move (TSP), or an ordinal over the jobs or exams and their other
  machines or timeslots (assignment, exam). An empty neighborhood throws `EmptyNeighborhood`.
  `FeasibleMove` is the EasyLocal 4 `is_valid`.
- Algorithms: `sd` is `SteepestDescent`; `fd` is `FirstDescent`; `sa` is `SimulatedAnnealing` with
  `start_temperature`, `min_temperature`, `cooling_rate` and
  `max_neighbors_sampled` set (no computed start temperature, no
  accepted-ratio cut-off); no runner has a `max_evaluations`, so the
  evaluations are not limited (since 3.4.1).

## Differences

- `fd`: EasyLocal 3 `FirstDescent` starts each scan from a random move and
  wraps around to it (`SelectRandomFirst`, which compares moves with
  `operator<`), while EasyLocal 4 `FirstImprovement` restarts every scan from
  the first move: the two follow different trajectories.
- `sd`: `SelectBest` breaks ties among the best moves at random, EasyLocal 4
  `BestImprovement` keeps the first one, so the trajectories diverge at the
  first tie.
- `sa`: the random streams differ (EasyLocal 3 draws from linear congruential
  engines sized to the range, and `std::uniform_int_distribution` differs
  between libc++ and libstdc++);
  the schedule and the number of evaluations are the same. An EasyLocal 3
  iteration is one accepted move (or the end of a temperature), not one
  sampled move.
- Counters: EasyLocal 3 counts the final iteration that finds no improving
  move, and does not count the evaluation of the initial solution, so on the
  same trajectory it reports one iteration more and one evaluation less than
  EasyLocal 4.
