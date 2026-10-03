// Delta evaluators of the two cost components of the EasyLocal assignment
// example (examples/assignment), which binds none since the examples have no
// whole-solution deltas.
//
// ReassignCapacityDeltaEvaluator is the one the example had until EasyLocal
// commit 21bc016: it recomputes the loads of the two machines in O(jobs). The
// infrastructure benchmarks bind it so that their assignment workload stays a
// light, allocation-free evaluation of each move; the EL3-versus-EL4 driver
// binds it in the `mixed` and `all` delta modes.
//
// ReassignLoadImbalanceDeltaEvaluator, for the `all` delta mode, recomputes
// every machine load in O(jobs) and the imbalance after the move in
// O(machines); el3_vs_el4/el3/assignment.hh has the same delta.
#pragma once

#include "assignment/cost_components.hpp"
#include "assignment/move.hpp"

#include <cassert>
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace benchmarks::assignment
{

using ::assignment::AssignmentInstance;
using ::assignment::AssignmentSolution;
using ::assignment::CapacityValue;
using ::assignment::machine_id;
using ::assignment::quantity_type;
using ::assignment::ReassignJobMove;

struct CapacityDelta
{
    std::int64_t overloaded_machines{};
    std::int64_t total_overload{};

    bool operator==(const CapacityDelta&) const = default;
};

constexpr CapacityValue operator+(CapacityValue value, CapacityDelta delta)
{
    return CapacityValue{
        .overloaded_machines = value.overloaded_machines + delta.overloaded_machines,
        .total_overload = value.total_overload + delta.total_overload,
    };
}

// The load of one machine, scanning every job.
inline quantity_type machine_load(
    const AssignmentInstance& instance,
    const AssignmentSolution& solution,
    machine_id machine)
{
    assert(solution.assignment.size() == instance.demand.size());
    quantity_type load = 0;
    for (std::size_t job = 0; job < solution.assignment.size(); ++job)
        if (solution.assignment[job] == machine)
            load += instance.demand[job];
    return load;
}

class ReassignCapacityDeltaEvaluator
{
public:
    explicit ReassignCapacityDeltaEvaluator(const AssignmentInstance& instance)
        : instance_{instance}
    {
    }

    CapacityDelta delta_evaluate(
        const AssignmentSolution& solution,
        const ReassignJobMove& move) const
    {
        using ::assignment::overload;
        assert(move.job < solution.assignment.size());
        assert(move.destination < instance_.capacity.size());

        const auto source = solution.assignment[move.job];
        assert(source != move.destination);

        const auto demand = instance_.demand[move.job];
        const auto source_load = machine_load(instance_, solution, source);
        const auto destination_load = machine_load(instance_, solution, move.destination);

        const auto source_before = overload(source_load, instance_.capacity[source]);
        const auto destination_before =
            overload(destination_load, instance_.capacity[move.destination]);
        const auto source_after = overload(source_load - demand, instance_.capacity[source]);
        const auto destination_after =
            overload(destination_load + demand, instance_.capacity[move.destination]);

        return CapacityDelta{
            .overloaded_machines = static_cast<std::int64_t>(source_after > 0)
                + static_cast<std::int64_t>(destination_after > 0)
                - static_cast<std::int64_t>(source_before > 0)
                - static_cast<std::int64_t>(destination_before > 0),
            .total_overload =
                source_after + destination_after - source_before - destination_before,
        };
    }

private:
    const AssignmentInstance& instance_;
};

class ReassignLoadImbalanceDeltaEvaluator
{
public:
    explicit ReassignLoadImbalanceDeltaEvaluator(const AssignmentInstance& instance)
        : instance_{instance}
    {
    }

    std::int64_t delta_evaluate(
        const AssignmentSolution& solution,
        const ReassignJobMove& move) const
    {
        assert(solution.assignment.size() == instance_.demand.size());
        assert(move.job < solution.assignment.size());
        assert(move.destination < instance_.capacity.size());

        std::vector<quantity_type> load(instance_.capacity.size(), quantity_type{0});
        for (std::size_t job = 0; job < solution.assignment.size(); ++job)
            load[solution.assignment[job]] += instance_.demand[job];

        const auto [minimum_before, maximum_before] = std::ranges::minmax_element(load);
        const auto before = *maximum_before - *minimum_before;

        const auto demand = instance_.demand[move.job];
        load[solution.assignment[move.job]] -= demand;
        load[move.destination] += demand;
        const auto [minimum_after, maximum_after] = std::ranges::minmax_element(load);
        const auto after = *maximum_after - *minimum_after;

        return after - before;
    }

private:
    const AssignmentInstance& instance_;
};

} // namespace benchmarks::assignment
