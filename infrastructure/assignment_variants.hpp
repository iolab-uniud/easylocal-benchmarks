#pragma once

#include "generator.hpp"
#include "std_generator_support.hpp"

#include "assignment/neighborhood_explorer.hpp"

#include <cassert>
#include <cstddef>

namespace easylocal::benchmark::neighborhood_traversal::assignment
{

using ::assignment::ReassignJobMove;
using ::assignment::AssignmentSolution;
using ::assignment::AssignmentSolutionManager;
using ::assignment::machine_id;

// The reassign explorer with the cursor protocol of EasyLocal 3 (first_move and
// next_move), as the Assignment example wrote it until EasyLocal
// 4.0.0-alpha.1: the same moves, in the same order, as the example's moves()
// generator.
class CursorNeighborhoodExplorer
    : public easylocal::neighborhood_explorer_base<
          AssignmentSolutionManager,
          ReassignJobMove>
{
public:
    using neighborhood_explorer_base::neighborhood_explorer_base;

    [[nodiscard]]
    auto is_valid(
        const AssignmentSolution& solution,
        const ReassignJobMove& move) const -> bool
    {
        return move.job < solution.assignment.size() &&
               move.destination < input().capacity.size() &&
               solution.assignment[move.job] != move.destination;
    }

    auto first_move(
        const AssignmentSolution& solution,
        ReassignJobMove& move) const -> bool
    {
        const auto machine_count = input().capacity.size();

        if (solution.assignment.empty() || machine_count < 2)
        {
            return false;
        }

        move.job = 0;
        move.destination = first_destination(solution, move.job);
        return true;
    }

    auto next_move(
        const AssignmentSolution& solution,
        ReassignJobMove& move) const -> bool
    {
        const auto machine_count = input().capacity.size();
        const auto current_machine = solution.assignment[move.job];

        for (auto destination = move.destination + 1;
             destination < machine_count;
             ++destination)
        {
            if (destination != current_machine)
            {
                move.destination = destination;
                return true;
            }
        }

        if (move.job + 1 < solution.assignment.size())
        {
            ++move.job;
            move.destination = first_destination(solution, move.job);
            return true;
        }

        return false;
    }

    void make_move(AssignmentSolution& solution, const ReassignJobMove& move) const noexcept
    {
        solution.assignment[move.job] = move.destination;
    }

private:
    [[nodiscard]]
    auto first_destination(
        const AssignmentSolution& solution,
        const std::size_t job) const noexcept -> machine_id
    {
        assert(input().capacity.size() >= 2);
        assert(job < solution.assignment.size());

        return solution.assignment[job] == 0 ? machine_id{1} : machine_id{0};
    }
};

class CoroutineNeighborhoodExplorer
{
public:
    using input_type = typename AssignmentSolutionManager::input_type;
    using solution_type = AssignmentSolution;
    using move_type = ReassignJobMove;

    explicit CoroutineNeighborhoodExplorer(
        const AssignmentSolutionManager& solution_manager) noexcept
        : solution_manager_{solution_manager}
    {
    }

    [[nodiscard]]
    auto input() const noexcept -> const input_type&
    {
        return solution_manager_.input();
    }

    [[nodiscard]]
    auto is_valid(
        const AssignmentSolution& solution,
        const ReassignJobMove& move) const -> bool
    {
        return move.job < solution.assignment.size() &&
               move.destination < solution_manager_.input().capacity.size() &&
               solution.assignment[move.job] != move.destination;
    }

    void make_move(AssignmentSolution& solution, const ReassignJobMove& move) const noexcept
    {
        solution.assignment[move.job] = move.destination;
    }

    [[nodiscard]]
    auto moves(const AssignmentSolution& solution) const -> generator<ReassignJobMove>
    {
        assert(solution_manager_.is_valid(solution));

        const auto machine_count =
            solution_manager_.input().capacity.size();

        for (std::size_t job = 0; job < solution.assignment.size(); ++job)
        {
            for (std::size_t destination = 0;
                 destination < machine_count;
                 ++destination)
            {
                if (destination != solution.assignment[job])
                {
                    co_yield ReassignJobMove{
                        .job = job,
                        .destination = destination,
                    };
                }
            }
        }
    }

private:
    const AssignmentSolutionManager& solution_manager_;
};

#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
class StdCoroutineNeighborhoodExplorer
{
public:
    using input_type = typename AssignmentSolutionManager::input_type;
    using solution_type = AssignmentSolution;
    using move_type = ReassignJobMove;

    explicit StdCoroutineNeighborhoodExplorer(
        const AssignmentSolutionManager& solution_manager) noexcept
        : solution_manager_{solution_manager}
    {
    }

    [[nodiscard]]
    auto input() const noexcept -> const input_type&
    {
        return solution_manager_.input();
    }

    [[nodiscard]]
    auto is_valid(
        const AssignmentSolution& solution,
        const ReassignJobMove& move) const -> bool
    {
        return move.job < solution.assignment.size() &&
               move.destination < solution_manager_.input().capacity.size() &&
               solution.assignment[move.job] != move.destination;
    }

    void make_move(AssignmentSolution& solution, const ReassignJobMove& move) const noexcept
    {
        solution.assignment[move.job] = move.destination;
    }

    [[nodiscard]]
    auto moves(const AssignmentSolution& solution) const -> std::generator<ReassignJobMove>
    {
        assert(solution_manager_.is_valid(solution));

        const auto machine_count =
            solution_manager_.input().capacity.size();

        for (std::size_t job = 0; job < solution.assignment.size(); ++job)
        {
            for (std::size_t destination = 0;
                 destination < machine_count;
                 ++destination)
            {
                if (destination != solution.assignment[job])
                {
                    co_yield ReassignJobMove{
                        .job = job,
                        .destination = destination,
                    };
                }
            }
        }
    }

private:
    const AssignmentSolutionManager& solution_manager_;
};
#endif

} // namespace easylocal::benchmark::neighborhood_traversal::assignment
