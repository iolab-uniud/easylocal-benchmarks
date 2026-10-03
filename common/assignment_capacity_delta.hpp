// A delta evaluator of the capacity component of the EasyLocal assignment
// example (examples/assignment), which binds none since the examples have no
// whole-solution deltas. The infrastructure benchmarks keep it so that their
// assignment workload stays what it was (a light, allocation-free evaluation
// of each move, so that the framework's own overhead is visible): it is the
// ReassignCapacityDeltaEvaluator the example had until EasyLocal commit
// 21bc016, which recomputes the loads of the two machines in O(jobs).
#pragma once

#include "assignment/cost_components.hpp"
#include "assignment/move.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

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

} // namespace benchmarks::assignment
