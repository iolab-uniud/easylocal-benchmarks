#pragma once

// The union of the TSP 2-opt and swap neighborhoods, as EasyLocal 4 gives it
// and as it would be without the dispatch of its iterator.
//
// A neighborhood_union enumerates its children through an iterator that holds
// a variant of theirs: advancing visits the variant, and building the move
// visits it again. ChildAfterChildExplorer yields the same moves, in the same
// order and of the same type, enumerating one child and then the other, so
// that the difference between the two variants is the cost of that dispatch
// and nothing else.

#include "tsp/neighborhood_explorer.hpp"
#include "tsp/swap_neighborhood_explorer.hpp"
#include "tsp/swap_tour_length_delta.hpp"
#include "tsp/tour_length_delta.hpp"

#include <easylocal/helpers/neighborhood_union.hpp>
#include <easylocal/helpers/recipes.hpp>
#include <easylocal/utils/generator.hpp>
#include <easylocal/utils/input_base.hpp>

#include <cstddef>
#include <utility>
#include <variant>

namespace easylocal::benchmark::neighborhood_traversal::tsp_union
{

using ::tsp::SwapCitiesNeighborhoodExplorer;
using ::tsp::Tour;
using ::tsp::TspSolutionManager;
using ::tsp::TwoOptNeighborhoodExplorer;

// The union of the two neighborhoods of the example, as a program composes it.
[[nodiscard]]
inline auto make_union(const TspSolutionManager& solution_manager)
{
    return easylocal::neighborhood_union(
               easylocal::neighborhood<TwoOptNeighborhoodExplorer>(),
               easylocal::neighborhood<SwapCitiesNeighborhoodExplorer>())
        .construct(solution_manager);
}

using union_explorer_type = decltype(make_union(
    std::declval<const TspSolutionManager&>()));
using union_move_type = typename union_explorer_type::move_type;

// The same moves as the union, enumerated child after child: the move of a
// child is tagged as the union tags it, and nothing visits a variant to
// produce the next one.
class ChildAfterChildExplorer
{
public:
    using input_type = typename TspSolutionManager::input_type;
    using solution_type = Tour;
    using move_type = union_move_type;

    explicit ChildAfterChildExplorer(const TspSolutionManager& solution_manager)
        : two_opt_{solution_manager},
          swap_{solution_manager},
          solution_manager_{solution_manager}
    {
    }

    [[nodiscard]]
    auto input() const noexcept -> const input_type&
    {
        return solution_manager_.input();
    }

    [[nodiscard]]
    auto is_valid(const Tour& solution, const move_type& move) const -> bool
    {
        return std::visit(
            [&](const auto& tagged) {
                if constexpr (tagged.index == 0)
                {
                    return two_opt_.is_valid(solution, tagged.value);
                }
                else
                {
                    return swap_.is_valid(solution, tagged.value);
                }
            },
            move);
    }

    void make_move(Tour& solution, const move_type& move) const
    {
        std::visit(
            [&](const auto& tagged) {
                if constexpr (tagged.index == 0)
                {
                    two_opt_.make_move(solution, tagged.value);
                }
                else
                {
                    swap_.make_move(solution, tagged.value);
                }
            },
            move);
    }

    [[nodiscard]]
    auto moves(const Tour& solution) const -> easylocal::generator<move_type>
    {
        using first_tagged = std::variant_alternative_t<0, move_type>;
        using second_tagged = std::variant_alternative_t<1, move_type>;

        for (const auto& move : two_opt_.moves(solution))
        {
            co_yield move_type{std::in_place_index<0>, first_tagged{move}};
        }
        for (const auto& move : swap_.moves(solution))
        {
            co_yield move_type{std::in_place_index<1>, second_tagged{move}};
        }
    }

private:
    TwoOptNeighborhoodExplorer two_opt_;
    SwapCitiesNeighborhoodExplorer swap_;
    const TspSolutionManager& solution_manager_;
};

// The tour length delta of both children in one component: it visits the
// tagged move and calls the delta of the child it comes from, which is what a
// union does through the binding it holds per child. Attached to
// ChildAfterChildExplorer, it gives a search the same deltas as the union,
// with the dispatch written by hand.
class UnionTourLengthDelta : public easylocal::input_base<::tsp::TspInstance>
{
public:
    using input_base::input_base;

    [[nodiscard]]
    auto delta_evaluate(const Tour& solution, const union_move_type& move) const
        -> double
    {
        return std::visit(
            [&](const auto& tagged) {
                if constexpr (tagged.index == 0)
                {
                    return two_opt_.delta_evaluate(solution, tagged.value);
                }
                else
                {
                    return swap_.delta_evaluate(solution, tagged.value);
                }
            },
            move);
    }

private:
    ::tsp::TwoOptTourLengthDelta two_opt_{input()};
    ::tsp::SwapTourLengthDelta swap_{input()};
};

} // namespace easylocal::benchmark::neighborhood_traversal::tsp_union
