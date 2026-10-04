#pragma once

#include "generator.hpp"
#include "std_generator_support.hpp"

#include "tsp/neighborhood_explorer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace easylocal::benchmark::neighborhood_traversal::tsp
{

using ::tsp::Tour;
using ::tsp::TspSolutionManager;
using ::tsp::TwoOptMove;

[[nodiscard]]
constexpr auto valid_edge_pair(
    const std::size_t city_count,
    const std::size_t first_edge,
    const std::size_t second_edge) noexcept -> bool
{
    return first_edge < second_edge &&
           second_edge < city_count &&
           second_edge != first_edge + 1 &&
           !(first_edge == 0 && second_edge + 1 == city_count);
}

// The 2-opt explorer with the cursor protocol of EasyLocal 3 (first_move and
// next_move), as the TSP example wrote it until EasyLocal 4.0.0-alpha.1: the
// same moves, in the same order, as the example's moves() generator.
class CursorNeighborhoodExplorer
    : public easylocal::neighborhood_explorer_base<TspSolutionManager, TwoOptMove>
{
public:
    using neighborhood_explorer_base::neighborhood_explorer_base;

    [[nodiscard]]
    auto is_valid(
        const Tour& solution,
        const TwoOptMove& move) const -> bool
    {
        return valid_edge_pair(
            solution.tour.size(),
            move.first_edge,
            move.second_edge);
    }

    auto first_move(const Tour& solution, TwoOptMove& move) const -> bool
    {
        return find_from(solution.tour.size(), 0, 1, move);
    }

    auto next_move(const Tour& solution, TwoOptMove& move) const -> bool
    {
        return find_from(
            solution.tour.size(),
            move.first_edge,
            move.second_edge + 1,
            move);
    }

    void make_move(Tour& solution, const TwoOptMove& move) const noexcept
    {
        const auto first = static_cast<std::ptrdiff_t>(move.first_edge + 1);
        const auto last = static_cast<std::ptrdiff_t>(move.second_edge + 1);
        std::reverse(
            solution.tour.begin() + first,
            solution.tour.begin() + last);
    }

private:
    static auto find_from(
        const std::size_t city_count,
        const std::size_t initial_first_edge,
        const std::size_t initial_second_edge,
        TwoOptMove& move) noexcept -> bool
    {
        for (auto first_edge = initial_first_edge;
             first_edge < city_count;
             ++first_edge)
        {
            const auto second_begin = first_edge == initial_first_edge
                ? initial_second_edge
                : first_edge + 1;

            for (auto second_edge = second_begin;
                 second_edge < city_count;
                 ++second_edge)
            {
                if (valid_edge_pair(city_count, first_edge, second_edge))
                {
                    move = TwoOptMove{
                        .first_edge = first_edge,
                        .second_edge = second_edge,
                    };
                    return true;
                }
            }
        }

        return false;
    }
};

class CoroutineNeighborhoodExplorer
{
public:
    using input_type = typename TspSolutionManager::input_type;
    using solution_type = Tour;
    using move_type = TwoOptMove;

    explicit CoroutineNeighborhoodExplorer(
        const TspSolutionManager& solution_manager) noexcept
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
        const Tour& solution,
        const TwoOptMove& move) const -> bool
    {
        return valid_edge_pair(
            solution.tour.size(),
            move.first_edge,
            move.second_edge);
    }

    void make_move(Tour& solution, const TwoOptMove& move) const noexcept
    {
        const auto first = static_cast<std::ptrdiff_t>(move.first_edge + 1);
        const auto last = static_cast<std::ptrdiff_t>(move.second_edge + 1);
        std::reverse(
            solution.tour.begin() + first,
            solution.tour.begin() + last);
    }

    [[nodiscard]]
    auto moves(const Tour& solution) const -> generator<TwoOptMove>
    {
        assert(solution_manager_.is_valid(solution));
        const auto city_count = solution.tour.size();

        for (std::size_t first_edge = 0;
             first_edge < city_count;
             ++first_edge)
        {
            for (std::size_t second_edge = first_edge + 1;
                 second_edge < city_count;
                 ++second_edge)
            {
                if (valid_edge_pair(
                        city_count,
                        first_edge,
                        second_edge))
                {
                    co_yield TwoOptMove{
                        .first_edge = first_edge,
                        .second_edge = second_edge,
                    };
                }
            }
        }
    }

private:
    const TspSolutionManager& solution_manager_;
};

#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
class StdCoroutineNeighborhoodExplorer
{
public:
    using input_type = typename TspSolutionManager::input_type;
    using solution_type = Tour;
    using move_type = TwoOptMove;

    explicit StdCoroutineNeighborhoodExplorer(
        const TspSolutionManager& solution_manager) noexcept
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
        const Tour& solution,
        const TwoOptMove& move) const -> bool
    {
        return valid_edge_pair(
            solution.tour.size(),
            move.first_edge,
            move.second_edge);
    }

    void make_move(Tour& solution, const TwoOptMove& move) const noexcept
    {
        const auto first = static_cast<std::ptrdiff_t>(move.first_edge + 1);
        const auto last = static_cast<std::ptrdiff_t>(move.second_edge + 1);
        std::reverse(
            solution.tour.begin() + first,
            solution.tour.begin() + last);
    }

    [[nodiscard]]
    auto moves(const Tour& solution) const -> std::generator<TwoOptMove>
    {
        assert(solution_manager_.is_valid(solution));
        const auto city_count = solution.tour.size();

        for (std::size_t first_edge = 0;
             first_edge < city_count;
             ++first_edge)
        {
            for (std::size_t second_edge = first_edge + 1;
                 second_edge < city_count;
                 ++second_edge)
            {
                if (valid_edge_pair(
                        city_count,
                        first_edge,
                        second_edge))
                {
                    co_yield TwoOptMove{
                        .first_edge = first_edge,
                        .second_edge = second_edge,
                    };
                }
            }
        }
    }

private:
    const TspSolutionManager& solution_manager_;
};
#endif

} // namespace easylocal::benchmark::neighborhood_traversal::tsp
