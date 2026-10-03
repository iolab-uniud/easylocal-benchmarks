// EasyLocal 3 port of the EasyLocal 4 assignment example (examples/assignment):
// reassign one job to another machine; hard cost = total overload, soft cost =
// load imbalance. As in the example, neither has a delta: both are evaluated on
// a copy of the solution with the move applied.
#pragma once

#include "helpers/solutionmanager.hh"
#include "helpers/costcomponent.hh"
#include "helpers/deltacostcomponent.hh"
#include "helpers/neighborhoodexplorer.hh"
#include "utils/random.hh"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace assignment
{

using namespace EasyLocal::Core;

// long: HARD_WEIGHT * overload + imbalance must not overflow.
using CFtype = long;
using CostStructure = DefaultCostStructure<CFtype>;
using quantity = long;

// Same file format as examples/assignment/instance_io.hpp: jobs, machines,
// the demand of each job, the capacity of each machine.
class Input
{
public:
    explicit Input(const std::string& path)
    {
        std::ifstream is(path);
        if (!is)
            throw std::runtime_error("cannot open assignment instance: " + path);
        std::size_t jobs = 0, machines = 0;
        if (!(is >> jobs >> machines))
            throw std::runtime_error("invalid assignment instance header");
        demand.resize(jobs);
        capacity.resize(machines);
        for (auto& d : demand)
            if (!(is >> d) || d < 0)
                throw std::runtime_error("invalid assignment demand data");
        for (auto& c : capacity)
            if (!(is >> c) || c < 0)
                throw std::runtime_error("invalid assignment capacity data");
        if (jobs != 0 && machines == 0)
            throw std::runtime_error("assignment instance has jobs but no machines");
    }

    std::vector<quantity> demand;
    std::vector<quantity> capacity;
};

class Assignment
{
public:
    explicit Assignment(const Input& in) : assignment(in.demand.size()) {}
    std::vector<std::size_t> assignment; // machine of each job
};

inline std::ostream& operator<<(std::ostream& os, const Assignment& st)
{
    for (std::size_t i = 0; i < st.assignment.size(); ++i)
        os << (i ? " " : "") << st.assignment[i];
    return os;
}

class ReassignJob
{
public:
    std::size_t job = 0, destination = 0;
};

inline bool operator==(const ReassignJob& a, const ReassignJob& b)
{
    return a.job == b.job && a.destination == b.destination;
}
inline bool operator!=(const ReassignJob& a, const ReassignJob& b) { return !(a == b); }
inline bool operator<(const ReassignJob& a, const ReassignJob& b)
{
    return a.job < b.job || (a.job == b.job && a.destination < b.destination);
}
inline std::ostream& operator<<(std::ostream& os, const ReassignJob& mv)
{
    return os << "job " << mv.job << " -> machine " << mv.destination;
}

inline quantity Overload(quantity load, quantity capacity)
{
    return std::max(quantity{0}, load - capacity);
}

class AssignmentSolutionManager
    : public SolutionManager<Input, Assignment, CostStructure>
{
public:
    explicit AssignmentSolutionManager(const Input& in)
        : SolutionManager<Input, Assignment, CostStructure>(in, "AssignmentSolutionManager") {}

    void RandomState(Assignment& st) override
    {
        st.assignment.resize(in.demand.size());
        for (auto& machine : st.assignment)
            machine = Random::Uniform<std::size_t>(0, in.capacity.size() - 1);
    }

    bool CheckConsistency(const Assignment& st) const override
    {
        return st.assignment.size() == in.demand.size();
    }
};

// Hard: sum over the machines of max(0, load - capacity).
class TotalOverload : public CostComponent<Input, Assignment, CFtype>
{
public:
    explicit TotalOverload(const Input& in)
        : CostComponent<Input, Assignment, CFtype>(in, 1, true, "TotalOverload") {}

    CFtype ComputeCost(const Assignment& st) const override
    {
        std::vector<quantity> load(in.capacity.size(), 0);
        for (std::size_t job = 0; job < st.assignment.size(); ++job)
            load[st.assignment[job]] += in.demand[job];
        CFtype total = 0;
        for (std::size_t machine = 0; machine < load.size(); ++machine)
            total += Overload(load[machine], in.capacity[machine]);
        return total;
    }

    void PrintViolations(const Assignment&, std::ostream&) const override {}
};

// Soft: max(load) - min(load).
class LoadImbalance : public CostComponent<Input, Assignment, CFtype>
{
public:
    explicit LoadImbalance(const Input& in)
        : CostComponent<Input, Assignment, CFtype>(in, 1, false, "LoadImbalance") {}

    CFtype ComputeCost(const Assignment& st) const override
    {
        if (in.capacity.empty())
            return 0;
        std::vector<quantity> load(in.capacity.size(), 0);
        for (std::size_t job = 0; job < st.assignment.size(); ++job)
            load[st.assignment[job]] += in.demand[job];
        const auto [minimum, maximum] = std::minmax_element(load.begin(), load.end());
        return *maximum - *minimum;
    }

    void PrintViolations(const Assignment&, std::ostream&) const override {}
};

class ReassignJobNeighborhoodExplorer
    : public NeighborhoodExplorer<Input, Assignment, ReassignJob, CostStructure>
{
public:
    ReassignJobNeighborhoodExplorer(const Input& in, AssignmentSolutionManager& sm)
        : NeighborhoodExplorer<Input, Assignment, ReassignJob, CostStructure>(in, sm, "Reassign job") {}

    bool FeasibleMove(const Assignment& st, const ReassignJob& mv) const override
    {
        return mv.job < st.assignment.size() && mv.destination < in.capacity.size() &&
               st.assignment[mv.job] != mv.destination;
    }

    // Uniform ordinal over jobs x (machines - 1), skipping the current machine.
    void RandomMove(const Assignment& st, ReassignJob& mv) const override
    {
        const auto alternatives = in.capacity.size() > 0 ? in.capacity.size() - 1 : 0;
        const auto count = st.assignment.size() * alternatives;
        if (count == 0)
            throw EmptyNeighborhood();
        const auto ordinal = Random::Uniform<std::size_t>(0, count - 1);
        mv.job = ordinal / alternatives;
        const auto offset = ordinal % alternatives;
        const auto current = st.assignment[mv.job];
        mv.destination = offset < current ? offset : offset + 1;
    }

    void FirstMove(const Assignment& st, ReassignJob& mv) const override
    {
        if (st.assignment.empty() || in.capacity.size() < 2)
            throw EmptyNeighborhood();
        mv.job = 0;
        mv.destination = FirstDestination(st, 0);
    }

    bool NextMove(const Assignment& st, ReassignJob& mv) const override
    {
        const auto current = st.assignment[mv.job];
        for (auto destination = mv.destination + 1; destination < in.capacity.size(); ++destination)
            if (destination != current)
            {
                mv.destination = destination;
                return true;
            }
        if (mv.job + 1 < st.assignment.size())
        {
            mv.job = mv.job + 1;
            mv.destination = FirstDestination(st, mv.job);
            return true;
        }
        return false;
    }

    void MakeMove(Assignment& st, const ReassignJob& mv) const override
    {
        st.assignment[mv.job] = mv.destination;
    }

private:
    static std::size_t FirstDestination(const Assignment& st, std::size_t job)
    {
        return st.assignment[job] == 0 ? 1 : 0;
    }
};

} // namespace assignment
