// EasyLocal 4 side of the EL3-versus-EL4 benchmark (see README.md).
//
//   el4_comparison --problem tsp|tsp-union|assignment|exam --instance FILE
//       --initial FILE --algorithm sd|fd|sa --seed N
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
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
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
void run_tsp(const Options& options)
{
    const auto input = el::load_input<tsp::TspInstance>(options.instance);
    tsp::Tour initial;
    initial.tour = read_values(options.initial);
    const auto sm = el::solution_manager<tsp::TspSolutionManager>()
                  | el::component<tsp::TourLengthComponent>();
    const auto neighborhood = el::neighborhood<tsp::TwoOptNeighborhoodExplorer>();
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
// delta only; none: no delta. In mixed the union has a component that one
// child has no delta for, which EasyLocal 4 then evaluates in full for the
// moves of both children, while EasyLocal 3 keeps using the delta of the
// child that has one: the trajectory is the same, the work is not.
void run_tsp_union(const Options& options)
{
    const auto input = el::load_input<tsp::TspInstance>(options.instance);
    tsp::Tour initial;
    initial.tour = read_values(options.initial);
    const auto sm = el::solution_manager<tsp::TspSolutionManager>()
                  | el::component<tsp::TourLengthComponent>();
    const auto two_opt = el::neighborhood<tsp::TwoOptNeighborhoodExplorer>()
                       | el::delta<tsp::TourLengthComponent, tsp::TwoOptTourLengthDelta>();
    const auto& mode = options.delta_mode;
    if (mode == "all")
        run(options, input, initial, sm,
            el::neighborhood_union(
                two_opt,
                el::neighborhood<tsp::SwapCitiesNeighborhoodExplorer>()
                    | el::delta<tsp::TourLengthComponent, tsp::SwapTourLengthDelta>())
                | el::random_biases(1.0, 1.0));
    else if (mode == "mixed")
        run(options, input, initial, sm,
            el::neighborhood_union(
                two_opt, el::neighborhood<tsp::SwapCitiesNeighborhoodExplorer>())
                | el::random_biases(1.0, 1.0));
    else if (mode == "none")
        run(options, input, initial, sm,
            el::neighborhood_union(
                el::neighborhood<tsp::TwoOptNeighborhoodExplorer>(),
                el::neighborhood<tsp::SwapCitiesNeighborhoodExplorer>())
                | el::random_biases(1.0, 1.0));
    else
        unsupported_mode(options);
}

// EasyLocal 3 adds its hard costs with weight HARD_WEIGHT = 1000.
// none (the example's configuration): no delta; mixed: the capacity delta
// only; all: the capacity and the load-imbalance deltas.
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
        el::neighborhood<assignment::ReassignJobNeighborhoodExplorer>();
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
    const auto neighborhood = el::neighborhood<exam::MoveExamNeighborhoodExplorer>();
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
        if (options.problem == "tsp")
            run_tsp(options);
        else if (options.problem == "tsp-union")
            run_tsp_union(options);
        else if (options.problem == "assignment")
            run_assignment(options);
        else if (options.problem == "exam")
            run_exam(options);
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
