// EasyLocal 4 side of the EL3-versus-EL4 benchmark (see README.md).
//
//   el4_comparison --problem tsp|tsp-union|assignment|exam --instance FILE
//       --initial FILE --algorithm sd|fd|rfd|sa --seed N
//       --delta-mode all|mixed|none
//       [--sa-start-temperature T --sa-min-temperature T
//        --sa-cooling-rate R --sa-samples N]
//
// Prints one line, initial_cost,final_cost,evaluations,iterations,seconds;
// seconds time the search only, not reading the files or building services.
// `el4_comparison --describe` prints instead, as key,value lines, the compiler,
// standard library and flags the driver was built with (the platforms
// benchmark records them next to its results).
// The models are the examples' own (EASYLOCAL_SOURCE_DIR/examples); the costs
// are the weighted sums of the EasyLocal 3 ports, so that both frameworks
// optimize the same function.
//
// --delta-mode chooses which cost components have a delta evaluator: all of
// them (all), some (mixed) or none, when every move is evaluated on a
// candidate solution. The examples themselves run all for TSP, none for
// assignment, mixed for exam. TSP has a single component, hence no mixed
// mode. The deltas the examples lack are in ../../common. tsp-union searches
// the TSP with the union of the 2-opt and the swap neighborhoods of the
// example, which have a delta each (all), only 2-opt (mixed) or none (none).

#include <easylocal/app/io.hpp>
#include <easylocal/helpers/neighborhood_union.hpp>
#include <easylocal/helpers/recipes.hpp>
#include <easylocal/runners/best_improvement.hpp>
#include <easylocal/runners/first_improvement.hpp>
#include <easylocal/utils/termination.hpp>
#include <easylocal/runners/runner.hpp>
#include <easylocal/runners/simulated_annealing.hpp>

// The examples of the EasyLocal checkout (EASYLOCAL_SOURCE_DIR), included by
// path because the three use the same file names.
#include "assignment/cost_components.hpp"
#if __has_include("assignment/instance_io.hpp")
// Up to v4.0.0-alpha.1 the assignment example read its instance with a free
// read_input; since EasyLocal 028e574 AssignmentInstance::read does.
#include "assignment/instance_io.hpp"
#endif
#include "assignment/neighborhood_explorer.hpp"
#include "exam_timetabling/cost_components.hpp"
#include "exam_timetabling/cost_deltas.hpp"
#include "exam_timetabling/neighborhood_explorer.hpp"
#include "tsp/neighborhood_explorer.hpp"
#include "tsp/swap_neighborhood_explorer.hpp"
#include "tsp/swap_tour_length_delta.hpp"
#include "tsp/tour_length_component.hpp"
#include "tsp/tour_length_delta.hpp"

#include "assignment_deltas.hpp"
#include "exam_timetabling_deltas.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <version>

#ifndef EL4_CXXFLAGS
#define EL4_CXXFLAGS ""
#endif

namespace
{

// Stringifies a macro's value (its expansion, not its name).
#define EL4_STRINGIFY_VALUE(x) EL4_STRINGIFY(x)
#define EL4_STRINGIFY(x) #x

// The toolchain of this binary, from the predefined macros: what CMake chose
// may differ from what the compiler reports about itself.
void describe()
{
#if defined(__clang__) && defined(__apple_build_version__)
    std::cout << "compiler,appleclang\n"
              << "compiler_version," << __clang_version__ << '\n';
#elif defined(__clang__) && defined(_MSC_VER)
    std::cout << "compiler,clang-cl\n"
              << "compiler_version," << __clang_version__ << '\n';
#elif defined(__clang__)
    std::cout << "compiler,clang\n"
              << "compiler_version," << __clang_version__ << '\n';
#elif defined(__GNUC__)
    std::cout << "compiler,gcc\n"
              << "compiler_version," << __VERSION__ << '\n';
#elif defined(_MSC_VER)
    std::cout << "compiler,msvc\n"
              << "compiler_version," << EL4_STRINGIFY_VALUE(_MSC_FULL_VER) << '\n';
#else
    std::cout << "compiler,unknown\ncompiler_version,unknown\n";
#endif
#if defined(_LIBCPP_VERSION)
    std::cout << "stdlib,libc++\n"
              << "stdlib_version," << EL4_STRINGIFY_VALUE(_LIBCPP_VERSION) << '\n';
#elif defined(__GLIBCXX__)
    std::cout << "stdlib,libstdc++\n"
              << "stdlib_version," << EL4_STRINGIFY_VALUE(__GLIBCXX__) << '\n';
#elif defined(_MSVC_STL_VERSION)
    std::cout << "stdlib,msvc-stl\n"
              << "stdlib_version," << EL4_STRINGIFY_VALUE(_MSVC_STL_UPDATE) << '\n';
#else
    std::cout << "stdlib,unknown\nstdlib_version,unknown\n";
#endif
    std::cout << "cxxflags," << EL4_CXXFLAGS << '\n'
              << "cpp_standard," << __cplusplus << '\n';
}

namespace el = easylocal;
namespace exam = exam_timetabling;

// The delta of the consecutive-exam component, under its name of the measured
// EasyLocal: 4.0.0-alpha.3 renamed ConsecutiveExamDeltaEvaluator.
#if __has_include(<easylocal/testing/delta_cost_component.hpp>)
using ConsecutiveExamDelta = exam::ConsecutiveExamDelta;
#else
using ConsecutiveExamDelta = exam::ConsecutiveExamDeltaEvaluator;
#endif

struct Options
{
    std::string problem;
    std::string instance;
    std::string initial;
    std::string algorithm;
    std::string delta_mode;
    std::uint64_t seed{1};
    el::runners::temperature::ClassicParameters annealing{};
};

auto parse(int argc, char* argv[]) -> Options
{
    std::map<std::string, std::string> values;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        values[argv[index]] = argv[index + 1];
    }
    auto required = [&](const std::string& flag) {
        const auto found = values.find(flag);
        if (found == values.end())
        {
            throw std::runtime_error{"missing " + flag};
        }
        return found->second;
    };
    Options options{
        .problem = required("--problem"),
        .instance = required("--instance"),
        .initial = required("--initial"),
        .algorithm = required("--algorithm"),
        .delta_mode = required("--delta-mode"),
        .seed = std::stoull(required("--seed")),
    };
    if (options.algorithm == "sa")
    {
        options.annealing = {
            .initial_temperature = std::stod(required("--sa-start-temperature")),
            .final_temperature = std::stod(required("--sa-min-temperature")),
            .cooling_rate = std::stod(required("--sa-cooling-rate")),
            .samples_per_temperature = std::stoull(required("--sa-samples")),
        };
    }
    return options;
}

auto read_values(const std::string& path) -> std::vector<std::size_t>
{
    std::ifstream in{path};
    if (!in)
    {
        throw std::runtime_error{"cannot open " + path};
    }
    std::vector<std::size_t> values;
    for (std::size_t value{}; in >> value;)
    {
        values.push_back(value);
    }
    return values;
}

// The explorers of the examples enumerate with a generator, which cannot be
// positioned: `rfd` needs the cursor of EasyLocal 3, first_move/next_move, to
// start its scan at a drawn move without enumerating the moves before it.
// Each cursor derives from the example's explorer, so the moves, their order,
// is_valid, make_move and the deltas are the example's own; only `rfd` uses
// them, since the cursor would otherwise win over moves() and change what the
// other algorithms measure.

class CursorTwoOpt : public tsp::TwoOptNeighborhoodExplorer
{
public:
    using TwoOptNeighborhoodExplorer::TwoOptNeighborhoodExplorer;

    auto first_move(const tsp::Tour& solution, tsp::TwoOptMove& move) const -> bool
    {
        move = tsp::TwoOptMove{0, 1};
        return advance(solution, move);
    }

    auto next_move(const tsp::Tour& solution, tsp::TwoOptMove& move) const -> bool
    {
        ++move.second_edge;
        return advance(solution, move);
    }

private:
    // The next valid pair at or after the one in move, in the order of the
    // example's moves(): every i < j with j >= i + 2.
    auto advance(const tsp::Tour& solution, tsp::TwoOptMove& move) const -> bool
    {
        const auto n = solution.tour.size();
        for (; move.first_edge < n; ++move.first_edge, move.second_edge = 0)
        {
            if (move.second_edge < move.first_edge + 2)
            {
                move.second_edge = move.first_edge + 2;
            }
            for (; move.second_edge < n; ++move.second_edge)
            {
                if (is_valid(solution, move))
                {
                    return true;
                }
            }
        }
        return false;
    }
};

class CursorSwapCities : public tsp::SwapCitiesNeighborhoodExplorer
{
public:
    using SwapCitiesNeighborhoodExplorer::SwapCitiesNeighborhoodExplorer;

    auto first_move(const tsp::Tour& solution, tsp::SwapCitiesMove& move) const -> bool
    {
        move = tsp::SwapCitiesMove{0, 1};
        return move.second_position < solution.tour.size();
    }

    auto next_move(const tsp::Tour& solution, tsp::SwapCitiesMove& move) const -> bool
    {
        const auto n = solution.tour.size();
        if (++move.second_position < n)
        {
            return true;
        }
        move.first_position += 1;
        move.second_position = move.first_position + 1;
        return move.second_position < n;
    }
};

class CursorReassignJob : public assignment::ReassignJobNeighborhoodExplorer
{
public:
    using ReassignJobNeighborhoodExplorer::ReassignJobNeighborhoodExplorer;

    auto first_move(
        const assignment::AssignmentSolution& solution,
        assignment::ReassignJobMove& move) const -> bool
    {
        move = assignment::ReassignJobMove{0, 0};
        return advance(solution, move);
    }

    auto next_move(
        const assignment::AssignmentSolution& solution,
        assignment::ReassignJobMove& move) const -> bool
    {
        ++move.destination;
        return advance(solution, move);
    }

private:
    auto advance(
        const assignment::AssignmentSolution& solution,
        assignment::ReassignJobMove& move) const -> bool
    {
        const auto machines = input().capacity.size();
        for (; move.job < solution.assignment.size(); ++move.job, move.destination = 0)
        {
            for (; move.destination < machines; ++move.destination)
            {
                if (solution.assignment[move.job] != move.destination)
                {
                    return true;
                }
            }
        }
        return false;
    }
};

class CursorMoveExam : public exam::MoveExamNeighborhoodExplorer
{
public:
    using MoveExamNeighborhoodExplorer::MoveExamNeighborhoodExplorer;

    auto first_move(const exam::ExamTimetable& solution, exam::MoveExam& move) const
        -> bool
    {
        move = exam::MoveExam{0, 0};
        return advance(solution, move);
    }

    auto next_move(const exam::ExamTimetable& solution, exam::MoveExam& move) const
        -> bool
    {
        ++move.destination;
        return advance(solution, move);
    }

private:
    auto advance(const exam::ExamTimetable& solution, exam::MoveExam& move) const -> bool
    {
        const auto timeslots = input().timeslot_count;
        for (; move.exam < solution.timeslot_by_exam.size();
             ++move.exam, move.destination = 0)
        {
            for (; move.destination < timeslots; ++move.destination)
            {
                if (solution.timeslot_by_exam[move.exam] != move.destination)
                {
                    return true;
                }
            }
        }
        return false;
    }
};

// A union of two neighborhoods has no cursor of its own: these walk the moves
// of the first child and then those of the second, which is the order the
// union enumerates and the order of the EasyLocal 3 SetUnion.
template<class NHE, class Solution>
concept union_of_two_cursors = requires(const NHE& neighborhood) {
    requires el::cursor_neighborhood_for<
        std::remove_cvref_t<decltype(neighborhood.template child<0>())>,
        Solution>;
    requires el::cursor_neighborhood_for<
        std::remove_cvref_t<decltype(neighborhood.template child<1>())>,
        Solution>;
};

template<std::size_t Index, class NHE, class Solution, class Move>
auto child_first(const NHE& neighborhood, const Solution& solution, Move& move) -> bool
{
    using tagged = std::variant_alternative_t<Index, Move>;
    typename tagged::value_type child_move{};
    if (!neighborhood.template child<Index>().first_move(solution, child_move))
    {
        return false;
    }
    move = Move{std::in_place_index<Index>, tagged{std::move(child_move)}};
    return true;
}

template<class NHE, class Solution, class Move>
    requires union_of_two_cursors<NHE, Solution>
auto scan_first(const NHE& neighborhood, const Solution& solution, Move& move) -> bool
{
    return child_first<0>(neighborhood, solution, move)
        || child_first<1>(neighborhood, solution, move);
}

template<class NHE, class Solution, class Move>
    requires union_of_two_cursors<NHE, Solution>
auto scan_next(const NHE& neighborhood, const Solution& solution, Move& move) -> bool
{
    if (move.index() == 0)
    {
        auto child_move = std::get<0>(move).value;
        if (neighborhood.template child<0>().next_move(solution, child_move))
        {
            move = Move{
                std::in_place_index<0>,
                std::variant_alternative_t<0, Move>{std::move(child_move)}};
            return true;
        }
        return child_first<1>(neighborhood, solution, move);
    }

    auto child_move = std::get<1>(move).value;
    if (!neighborhood.template child<1>().next_move(solution, child_move))
    {
        return false;
    }
    move = Move{
        std::in_place_index<1>,
        std::variant_alternative_t<1, Move>{std::move(child_move)}};
    return true;
}

template<class NHE, class Solution, class Move>
    requires el::cursor_neighborhood_for<NHE, Solution>
auto scan_first(const NHE& neighborhood, const Solution& solution, Move& move) -> bool
{
    return neighborhood.first_move(solution, move);
}

template<class NHE, class Solution, class Move>
    requires el::cursor_neighborhood_for<NHE, Solution>
auto scan_next(const NHE& neighborhood, const Solution& solution, Move& move) -> bool
{
    return neighborhood.next_move(solution, move);
}

// Whether the explorer can be scanned from a drawn move: `rfd` runs on the
// cursor explorers above, the other algorithms on the examples' own.
template<class NHE, class Solution>
concept scannable = requires(
    const NHE& neighborhood,
    const Solution& solution,
    typename NHE::move_type& move) {
    { scan_first(neighborhood, solution, move) } -> std::same_as<bool>;
    { scan_next(neighborhood, solution, move) } -> std::same_as<bool>;
};

// `rfd`: first improvement as EasyLocal 3's FirstDescent explores the
// neighborhood, so that the two frameworks walk the same trajectory. The scan
// starts at a move drawn at random and wraps around; it walks the cursor of
// the explorers above, so that it evaluates every move it enumerates, as
// EasyLocal 3's NextMove does.
//
// The library's own FirstImprovement (`fd`) restarts at the first move and
// scans again the moves the previous iterations rejected: on a large
// neighborhood that costs many more evaluations for the same number of
// iterations, and on a union, which enumerates its first child before its
// second, it evaluates almost only moves of the first child. A delta on that
// child alone then looks free, which is what `rfd` exists to measure
// honestly.
class RandomFirstImprovement
{
public:
    template<class Run, std::uniform_random_bit_generator RNG>
    auto run(Run& run, typename Run::solution_type solution, RNG& rng) const
    {
        auto current = run.start(solution);
        const auto& neighborhood = run.neighborhood_explorer();

        while (true)
        {
            const auto drawn = el::random_move(neighborhood, solution, rng);
            if (!drawn)
            {
                return run.finish(
                    std::move(solution),
                    current.cost(),
                    el::termination_reason::local_optimum);
            }

            const auto start = *drawn;
            auto move = start;
            bool improved = false;
            // Two wraps bound the scan: one is the circle back to the drawn
            // move, the second only happens if the enumeration never produces
            // it, which would otherwise loop forever.
            int wraps = 0;
            while (wraps < 2)
            {
                if (!scan_next(neighborhood, solution, move))
                {
                    ++wraps;
                    if (!scan_first(neighborhood, solution, move))
                    {
                        break;
                    }
                }
                if (wraps > 0 && move == start)
                {
                    break;
                }
                if (run.should_stop())
                {
                    return run.finish(std::move(solution), current.cost());
                }

                auto candidate = run.evaluate_move(solution, current, move);
                if (run.better(candidate.cost(), current.cost()))
                {
                    run.next_iteration();
                    run.commit_improvement(solution, current, std::move(candidate), move);
                    improved = true;
                    break;
                }
            }

            if (!improved)
            {
                return run.finish(
                    std::move(solution),
                    current.cost(),
                    el::termination_reason::local_optimum);
            }
        }
    }
};

// Runs the chosen algorithm on a SolutionManager and NeighborhoodExplorer
// recipe and prints the result line.
template<class Input, class Solution, class SM, class NHE>
void run(
    const Options& options,
    const Input& input,
    const Solution& initial,
    const SM& sm,
    const NHE& nhe)
{
    const auto initial_cost = sm.construct(input).evaluate(initial);

    auto report = [&](const auto& result, const auto elapsed) {
        std::cout << std::setprecision(17) << initial_cost << ','
                  << result.cost << ',' << result.evaluations << ','
                  << result.iterations << ','
                  << std::chrono::duration<double>(elapsed).count() << '\n';
    };
    auto timed = [&](auto& bound, auto&&... rng) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = bound.run(initial, rng...);
        report(result, std::chrono::steady_clock::now() - start);
    };

    if (options.algorithm == "sd")
    {
        auto runner = el::make_runner<el::runners::BestImprovement>() | sm | nhe;
        auto bound = runner.bind(input);
        timed(bound);
    }
    else if (options.algorithm == "fd")
    {
        auto runner = el::make_runner<el::runners::FirstImprovement>() | sm | nhe;
        auto bound = runner.bind(input);
        timed(bound);
    }
    else if (options.algorithm == "rfd")
    {
        using explorer_type = el::detail::service_t<std::remove_cvref_t<decltype(nhe)>>;
        if constexpr (scannable<explorer_type, Solution>)
        {
            auto runner = el::Runner{RandomFirstImprovement{}} | sm | nhe;
            auto bound = runner.bind(input);
            std::mt19937_64 rng{options.seed};
            timed(bound, rng);
        }
        else
        {
            throw std::runtime_error{"rfd needs the cursor explorers"};
        }
    }
    else if (options.algorithm == "sa")
    {
        using Classic = el::runners::temperature::Classic;
        auto runner = el::make_runner<el::runners::SimulatedAnnealing<Classic>>(
                          {.temperature = options.annealing})
                    | sm | nhe;
        auto bound = runner.bind(input);
        std::mt19937_64 rng{options.seed};
        timed(bound, rng);
    }
    else
    {
        throw std::runtime_error{"unknown algorithm " + options.algorithm};
    }
}

// The hard cost of the EasyLocal 3 port: the total overload.
struct TotalOverload
{
    auto operator()(const assignment::CapacityValue& value) const noexcept
        -> std::int64_t
    {
        return value.total_overload;
    }
};

[[noreturn]] void unsupported_mode(const Options& options)
{
    throw std::runtime_error{
        "unsupported --delta-mode " + options.delta_mode + " for " + options.problem};
}

// all (the example's configuration): the 2-opt delta; none: no delta.
template<class TwoOpt = tsp::TwoOptNeighborhoodExplorer>
void run_tsp(const Options& options)
{
    const auto input = el::load_input<tsp::TspInstance>(options.instance);
    tsp::Tour initial;
    initial.tour = read_values(options.initial);
    const auto sm = el::solution_manager<tsp::TspSolutionManager>()
                  | el::component<tsp::TourLengthComponent>();
    const auto neighborhood = el::neighborhood<TwoOpt>();
    const auto& mode = options.delta_mode;
    if (mode == "all")
        run(options, input, initial, sm,
            neighborhood
                | el::delta<tsp::TourLengthComponent, tsp::TwoOptTourLengthDelta>());
    else if (mode == "none")
        run(options, input, initial, sm, neighborhood);
    else
        unsupported_mode(options);
}

// The union of the 2-opt and the swap neighborhoods (neighborhood_union),
// the two drawn with the same bias and enumerated one after the other, as the
// EasyLocal 3 SetUnion. all: the delta of each neighborhood; mixed: the 2-opt
// delta only, so that the union has a component one child has no delta for;
// none: no delta.
template<
    class TwoOpt = tsp::TwoOptNeighborhoodExplorer,
    class SwapCities = tsp::SwapCitiesNeighborhoodExplorer>
void run_tsp_union(const Options& options)
{
    const auto input = el::load_input<tsp::TspInstance>(options.instance);
    tsp::Tour initial;
    initial.tour = read_values(options.initial);
    const auto sm = el::solution_manager<tsp::TspSolutionManager>()
                  | el::component<tsp::TourLengthComponent>();
    const auto two_opt = el::neighborhood<TwoOpt>()
                       | el::delta<tsp::TourLengthComponent, tsp::TwoOptTourLengthDelta>();
    const auto& mode = options.delta_mode;
    if (mode == "all")
        run(options, input, initial, sm,
            el::neighborhood_union(
                two_opt,
                el::neighborhood<SwapCities>()
                    | el::delta<tsp::TourLengthComponent, tsp::SwapTourLengthDelta>())
                | el::random_biases(1.0, 1.0));
    else if (mode == "mixed")
        run(options, input, initial, sm,
            el::neighborhood_union(
                two_opt, el::neighborhood<SwapCities>())
                | el::random_biases(1.0, 1.0));
    else if (mode == "none")
        run(options, input, initial, sm,
            el::neighborhood_union(
                el::neighborhood<TwoOpt>(),
                el::neighborhood<SwapCities>())
                | el::random_biases(1.0, 1.0));
    else
        unsupported_mode(options);
}

// EasyLocal 3 adds its hard costs with weight HARD_WEIGHT = 1000.
// none (the example's configuration): no delta; mixed: the capacity delta
// only; all: the capacity and the load-imbalance deltas.
template<class ReassignJob = assignment::ReassignJobNeighborhoodExplorer>
void run_assignment(const Options& options)
{
    namespace deltas = benchmarks::assignment;
    const auto input = el::load_input<assignment::AssignmentInstance>(options.instance);
    assignment::AssignmentSolution initial;
    initial.assignment = read_values(options.initial);
    const auto sm =
        el::solution_manager<assignment::AssignmentSolutionManager>()
        | el::cost::sum(
              el::cost::apply(TotalOverload{},
                              el::component<assignment::CapacityCostComponent>())
                  * 1000,
              el::component<assignment::LoadImbalanceCostComponent>());
    const auto neighborhood =
        el::neighborhood<ReassignJob>();
    const auto capacity = el::delta<assignment::CapacityCostComponent,
                                    deltas::ReassignCapacityDeltaEvaluator>();
    const auto& mode = options.delta_mode;
    if (mode == "none")
        run(options, input, initial, sm, neighborhood);
    else if (mode == "mixed")
        run(options, input, initial, sm, neighborhood | capacity);
    else if (mode == "all")
        run(options, input, initial, sm,
            neighborhood | capacity
                | el::delta<assignment::LoadImbalanceCostComponent,
                            deltas::ReassignLoadImbalanceDeltaEvaluator>());
    else
        unsupported_mode(options);
}

// mixed (the example's configuration): the conflict and consecutive-exam
// deltas, none for the timeslot load; all: the timeslot-load delta too; none:
// no delta.
template<class MoveExam = exam::MoveExamNeighborhoodExplorer>
void run_exam(const Options& options)
{
    const auto input = el::load_input<exam::ExamTimetablingInstance>(options.instance);
    exam::ExamTimetable initial;
    initial.timeslot_by_exam = read_values(options.initial);
    const auto sm = el::solution_manager<exam::ExamTimetablingSolutionManager>()
                  | el::cost::sum(
                        el::component<exam::StudentConflictComponent>() * 1000,
                        el::component<exam::ConsecutiveExamComponent>() * 10,
                        el::component<exam::TimeslotLoadComponent>());
    const auto neighborhood = el::neighborhood<MoveExam>();
    const auto mixed = neighborhood
                     | el::delta<exam::StudentConflictComponent>()
                     | el::delta<exam::ConsecutiveExamComponent,
                                 ConsecutiveExamDelta>();
    const auto& mode = options.delta_mode;
    if (mode == "mixed")
        run(options, input, initial, sm, mixed);
    else if (mode == "all")
        run(options, input, initial, sm,
            mixed
                | el::delta<exam::TimeslotLoadComponent,
                            benchmarks::exam_timetabling::TimeslotLoadDeltaEvaluator>());
    else if (mode == "none")
        run(options, input, initial, sm, neighborhood);
    else
        unsupported_mode(options);
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        if (argc == 2 && std::string{argv[1]} == "--describe")
        {
            describe();
            return 0;
        }
        const auto options = parse(argc, argv);
        // `rfd` scans with the cursor explorers, the others with the
        // examples' own, whose moves() the cursor would otherwise win over.
        const bool cursor = options.algorithm == "rfd";
        if (options.problem == "tsp")
            cursor ? run_tsp<CursorTwoOpt>(options) : run_tsp<>(options);
        else if (options.problem == "tsp-union")
            cursor ? run_tsp_union<CursorTwoOpt, CursorSwapCities>(options)
                   : run_tsp_union<>(options);
        else if (options.problem == "assignment")
            cursor ? run_assignment<CursorReassignJob>(options)
                   : run_assignment<>(options);
        else if (options.problem == "exam")
            cursor ? run_exam<CursorMoveExam>(options) : run_exam<>(options);
        else
            throw std::runtime_error{"unknown problem " + options.problem};
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
