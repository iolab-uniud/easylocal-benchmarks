// EasyLocal 4 side of the EL3-versus-EL4 benchmark (see README.md).
//
//   el4_comparison --problem tsp|assignment|exam --instance FILE
//       --initial FILE --algorithm sd|fd|sa --seed N
//       [--sa-start-temperature T --sa-min-temperature T
//        --sa-cooling-rate R --sa-samples N]
//
// Prints one line, initial_cost,final_cost,evaluations,iterations,seconds;
// seconds time the search only, not reading the files or building services.
// The models are the examples' own; the costs are the ones of the EasyLocal 3
// ports, so that both frameworks optimize the same function.

#include <easylocal/helpers/recipes.hpp>
#include <easylocal/runners/best_improvement.hpp>
#include <easylocal/runners/first_improvement.hpp>
#include <easylocal/runners/runner.hpp>
#include <easylocal/runners/simulated_annealing.hpp>

// The examples of the EasyLocal checkout (EASYLOCAL_SOURCE_DIR), included by
// path because the three use the same file names.
#include "assignment/capacity_delta.hpp"
#include "assignment/cost_components.hpp"
#include "assignment/instance_io.hpp"
#include "assignment/neighborhood_explorer.hpp"
#include "exam_timetabling/cost_components.hpp"
#include "exam_timetabling/cost_deltas.hpp"
#include "exam_timetabling/instance_io.hpp"
#include "exam_timetabling/neighborhood_explorer.hpp"
#include "tsp/instance_io.hpp"
#include "tsp/neighborhood_explorer.hpp"
#include "tsp/tour_length_delta.hpp"

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

namespace
{

namespace el = easylocal;
namespace assignment = easylocal::mwe::assignment;
namespace exam = easylocal::mwe::exam_timetabling;
namespace tsp = easylocal::mwe::tsp;

struct Options
{
    std::string problem;
    std::string instance;
    std::string initial;
    std::string algorithm;
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
        auto runner = el::make_runner<el::runners::BestImprovement>(
                          el::runners::BestImprovementParameters{})
                    | sm | nhe;
        auto bound = runner.bind(input);
        timed(bound);
    }
    else if (options.algorithm == "fd")
    {
        auto runner = el::make_runner<el::runners::FirstImprovement>(
                          el::runners::FirstImprovementParameters{})
                    | sm | nhe;
        auto bound = runner.bind(input);
        timed(bound);
    }
    else if (options.algorithm == "sa")
    {
        using Classic = el::runners::temperature::Classic;
        auto runner = el::make_runner<el::runners::SimulatedAnnealing<Classic>>(
                          Classic{options.annealing})
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

void run_tsp(const Options& options)
{
    const auto input = tsp::load_instance(options.instance);
    tsp::Tour initial;
    initial.tour = read_values(options.initial);
    run(options, input, initial,
        el::solution_manager<tsp::TspSolutionManager>()
            | el::cost::apply(tsp::TourLengthCost{},
                              el::component<tsp::TourLengthComponent>()),
        el::neighborhood<tsp::TwoOptNeighborhoodExplorer>()
            | el::delta<tsp::TourLengthComponent,
                        tsp::TwoOptTourLengthDeltaEvaluator>());
}

// EasyLocal 3 adds its hard costs with weight HARD_WEIGHT = 1000.
void run_assignment(const Options& options)
{
    const auto input = assignment::load_instance(options.instance);
    assignment::AssignmentSolution initial;
    initial.assignment = read_values(options.initial);
    run(options, input, initial,
        el::solution_manager<assignment::AssignmentSolutionManager>()
            | el::cost::sum(
                  el::cost::apply(TotalOverload{},
                                  el::component<assignment::CapacityCostComponent>())
                      * 1000,
                  el::component<assignment::LoadImbalanceCostComponent>()),
        el::neighborhood<assignment::ReassignJobNeighborhoodExplorer>()
            | el::delta<assignment::CapacityCostComponent,
                        assignment::ReassignCapacityDeltaEvaluator>());
}

void run_exam(const Options& options)
{
    const auto input = exam::load_instance(options.instance);
    exam::ExamTimetable initial;
    initial.timeslot_by_exam = read_values(options.initial);
    run(options, input, initial,
        el::solution_manager<exam::ExamTimetablingSolutionManager>()
            | el::cost::sum(
                  el::component<exam::StudentConflictComponent>() * 1000,
                  el::component<exam::ConsecutiveExamComponent>() * 10,
                  el::component<exam::TimeslotLoadComponent>()),
        el::neighborhood<exam::MoveExamNeighborhoodExplorer>()
            | el::delta<exam::StudentConflictComponent>()
            | el::delta<exam::ConsecutiveExamComponent,
                        exam::ConsecutiveExamDeltaEvaluator>()
            | el::delta<exam::TimeslotLoadComponent,
                        exam::TimeslotLoadDeltaEvaluator>());
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        const auto options = parse(argc, argv);
        if (options.problem == "tsp")
            run_tsp(options);
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
