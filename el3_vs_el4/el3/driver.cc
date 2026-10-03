// EasyLocal 3 (v3.3.1) side of the EL3-versus-EL4 benchmark; same command line
// and output as ../el4/driver.cpp:
//
//   el3_comparison --problem tsp|assignment|exam --instance FILE
//       --initial FILE --algorithm sd|fd|sa --seed N
//       [--sa-start-temperature T --sa-min-temperature T
//        --sa-cooling-rate R --sa-samples N]
//
// Prints one line, initial_cost,final_cost,evaluations,iterations,seconds;
// seconds time Go() only. An optional --output FILE (not in the EL4 driver)
// writes the final solution, for checking it.

#include "assignment.hh"
#include "exam.hh"
#include "tsp.hh"

#include "runners/firstdescent.hh"
#include "runners/simulatedannealing.hh"
#include "runners/steepestdescent.hh"
#include "utils/random.hh"

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using namespace EasyLocal::Core;

struct Options
{
    std::string problem, instance, initial, algorithm, output;
    unsigned int seed = 1;
    double start_temperature = 0.0, min_temperature = 0.0, cooling_rate = 0.0;
    unsigned int samples = 0;
};

Options Parse(int argc, char* argv[])
{
    std::map<std::string, std::string> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    auto required = [&](const std::string& flag) {
        const auto found = values.find(flag);
        if (found == values.end())
            throw std::runtime_error("missing " + flag);
        return found->second;
    };
    Options o;
    o.problem = required("--problem");
    o.instance = required("--instance");
    o.initial = required("--initial");
    o.algorithm = required("--algorithm");
    o.seed = static_cast<unsigned int>(std::stoul(required("--seed")));
    if (values.count("--output"))
        o.output = values["--output"];
    if (o.algorithm == "sa")
    {
        o.start_temperature = std::stod(required("--sa-start-temperature"));
        o.min_temperature = std::stod(required("--sa-min-temperature"));
        o.cooling_rate = std::stod(required("--sa-cooling-rate"));
        o.samples = static_cast<unsigned int>(std::stoul(required("--sa-samples")));
    }
    return o;
}

// Reads the whitespace-separated initial solution into `values`, checking
// its length and that every value is below `bound`.
void ReadValues(const std::string& path, std::vector<std::size_t>& values, std::size_t bound)
{
    std::ifstream is(path);
    if (!is)
        throw std::runtime_error("cannot open " + path);
    std::vector<std::size_t> read;
    for (std::size_t v; is >> v;)
        read.push_back(v);
    if (read.size() != values.size())
        throw std::runtime_error("wrong number of values in " + path);
    for (auto v : read)
        if (v >= bound)
            throw std::runtime_error("value out of range in " + path);
    values = read;
}

// Runs the chosen EasyLocal 3 runner with Go() from `initial` and prints the
// result line.
template <class Input, class Solution, class Move, class CostStructure>
void Run(const Options& o, const Input& in, const Solution& initial,
         SolutionManager<Input, Solution, CostStructure>& sm,
         NeighborhoodExplorer<Input, Solution, Move, CostStructure>& ne)
{
    const auto initial_cost = sm.CostFunctionComponents(initial).total;

    auto timed = [&](Runner<Input, Solution, CostStructure>& runner) {
        Solution s = initial;
        const auto start = std::chrono::steady_clock::now();
        const CostStructure result = runner.Go(s);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::cout << std::setprecision(17) << initial_cost << ',' << result.total << ','
                  << runner.Evaluations() << ',' << runner.Iteration() << ','
                  << std::chrono::duration<double>(elapsed).count() << '\n';
        if (!o.output.empty())
            std::ofstream(o.output) << s << '\n';
    };

    if (o.algorithm == "sd")
    {
        SteepestDescent<Input, Solution, Move, CostStructure> runner(in, sm, ne, "sd");
        timed(runner);
    }
    else if (o.algorithm == "fd")
    {
        FirstDescent<Input, Solution, Move, CostStructure> runner(in, sm, ne, "fd");
        timed(runner);
    }
    else if (o.algorithm == "sa")
    {
        SimulatedAnnealing<Input, Solution, Move, CostStructure> runner(in, sm, ne, "sa");
        runner.SetParameter("start_temperature", o.start_temperature);
        runner.SetParameter("min_temperature", o.min_temperature);
        runner.SetParameter("cooling_rate", o.cooling_rate);
        runner.SetParameter("neighbors_sampled", o.samples);
        timed(runner);
    }
    else
        throw std::runtime_error("unknown algorithm " + o.algorithm);
}

void RunTsp(const Options& o)
{
    const tsp::Input in(o.instance);
    tsp::Tour initial(in);
    ReadValues(o.initial, initial.tour, in.city_count);
    tsp::TspSolutionManager sm(in);
    tsp::TourLength length(in);
    sm.AddCostComponent(length);
    tsp::TwoOptNeighborhoodExplorer ne(in, sm);
    tsp::TwoOptTourLengthDelta delta(in, length);
    ne.AddDeltaCostComponent(delta);
    Run(o, in, initial, sm, ne);
}

void RunAssignment(const Options& o)
{
    const assignment::Input in(o.instance);
    assignment::Assignment initial(in);
    ReadValues(o.initial, initial.assignment, in.capacity.size());
    assignment::AssignmentSolutionManager sm(in);
    assignment::TotalOverload overload(in);
    assignment::LoadImbalance imbalance(in);
    sm.AddCostComponent(overload);
    sm.AddCostComponent(imbalance);
    assignment::ReassignJobNeighborhoodExplorer ne(in, sm);
    // No deltas, as in the EasyLocal 4 example: EasyLocal 3 wraps each full
    // cost component in a DeltaCostComponentAdapter (move applied to a copy).
    ne.AddCostComponent(overload);
    ne.AddCostComponent(imbalance);
    Run(o, in, initial, sm, ne);
}

void RunExam(const Options& o)
{
    const exam::Input in(o.instance);
    exam::Timetable initial(in);
    ReadValues(o.initial, initial.timeslot_by_exam, in.timeslot_count);
    exam::ExamSolutionManager sm(in);
    exam::StudentConflicts conflicts(in);
    exam::ConsecutiveExams consecutive(in);
    exam::TimeslotLoad load(in);
    sm.AddCostComponent(conflicts);
    sm.AddCostComponent(consecutive);
    sm.AddCostComponent(load);
    exam::MoveExamNeighborhoodExplorer ne(in, sm);
    exam::StudentConflictsDelta conflicts_delta(in, conflicts);
    exam::ConsecutiveExamsDelta consecutive_delta(in, consecutive);
    ne.AddDeltaCostComponent(conflicts_delta);
    ne.AddDeltaCostComponent(consecutive_delta);
    // No delta for the timeslot load, as in the EasyLocal 4 example.
    ne.AddCostComponent(load);
    Run(o, in, initial, sm, ne);
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        const auto o = Parse(argc, argv);
        Random::SetSeed(o.seed);
        if (o.problem == "tsp")
            RunTsp(o);
        else if (o.problem == "assignment")
            RunAssignment(o);
        else if (o.problem == "exam")
            RunExam(o);
        else
            throw std::runtime_error("unknown problem " + o.problem);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
