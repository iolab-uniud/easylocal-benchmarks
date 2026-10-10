// EasyLocal 3 port of the EasyLocal 4 TSP example (examples/tsp): tour length
// with the closing edge, the 2-opt neighborhood with its O(1) delta and the
// swap neighborhood with its O(1) delta, which the union benchmark joins.
#pragma once

#include "helpers/solutionmanager.hh"
#include "helpers/costcomponent.hh"
#include "helpers/deltacostcomponent.hh"
#include "helpers/neighborhoodexplorer.hh"
#include "utils/random.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tsp
{

using namespace EasyLocal::Core;

using CostStructure = DefaultCostStructure<double>;

// Same file format as examples/tsp/instance.hpp: the city count, then the
// full row-major distance matrix.
class Input
{
public:
    explicit Input(const std::string& path)
    {
        std::ifstream is(path);
        if (!is)
            throw std::runtime_error("cannot open TSP instance: " + path);
        if (!(is >> city_count) || city_count < 2)
            throw std::runtime_error("invalid TSP instance header");
        distances.resize(city_count * city_count);
        for (auto& d : distances)
            if (!(is >> d) || !std::isfinite(d) || d < 0.0)
                throw std::runtime_error("invalid TSP distance data");
    }

    double Distance(std::size_t from, std::size_t to) const
    {
        return distances[from * city_count + to];
    }

    std::size_t city_count = 0;
    std::vector<double> distances;
};

class Tour
{
public:
    explicit Tour(const Input& in) : tour(in.city_count) {}
    std::vector<std::size_t> tour;
};

inline std::ostream& operator<<(std::ostream& os, const Tour& st)
{
    for (std::size_t i = 0; i < st.tour.size(); ++i)
        os << (i ? " " : "") << st.tour[i];
    return os;
}

// Reverses the segment tour[first_edge + 1 .. second_edge].
class TwoOpt
{
public:
    std::size_t first_edge = 0, second_edge = 0;
};

inline bool operator==(const TwoOpt& a, const TwoOpt& b)
{
    return a.first_edge == b.first_edge && a.second_edge == b.second_edge;
}
inline bool operator!=(const TwoOpt& a, const TwoOpt& b) { return !(a == b); }
inline bool operator<(const TwoOpt& a, const TwoOpt& b)
{
    return a.first_edge < b.first_edge ||
           (a.first_edge == b.first_edge && a.second_edge < b.second_edge);
}
inline std::ostream& operator<<(std::ostream& os, const TwoOpt& mv)
{
    return os << "edge " << mv.first_edge << " <-> edge " << mv.second_edge;
}

class TspSolutionManager : public SolutionManager<Input, Tour, CostStructure>
{
public:
    explicit TspSolutionManager(const Input& in)
        : SolutionManager<Input, Tour, CostStructure>(in, "TspSolutionManager") {}

    void RandomState(Tour& st) override
    {
        st.tour.resize(in.city_count);
        std::iota(st.tour.begin(), st.tour.end(), std::size_t{0});
        Random::Shuffle(st.tour);
    }

    bool CheckConsistency(const Tour& st) const override
    {
        return st.tour.size() == in.city_count;
    }
};

class TourLength : public CostComponent<Input, Tour, double>
{
public:
    explicit TourLength(const Input& in)
        : CostComponent<Input, Tour, double>(in, 1.0, false, "TourLength") {}

    double ComputeCost(const Tour& st) const override
    {
        if (st.tour.empty())
            return 0.0;
        double total = 0.0;
        for (std::size_t position = 0; position < st.tour.size(); ++position)
            total += in.Distance(st.tour[position],
                                 st.tour[(position + 1) % st.tour.size()]);
        return total;
    }

    void PrintViolations(const Tour&, std::ostream&) const override {}
};

class TwoOptTourLengthDelta : public DeltaCostComponent<Input, Tour, TwoOpt, double>
{
public:
    TwoOptTourLengthDelta(const Input& in, TourLength& cc)
        : DeltaCostComponent<Input, Tour, TwoOpt, double>(in, cc, "TwoOptTourLengthDelta") {}

    double ComputeDeltaCost(const Tour& st, const TwoOpt& mv) const override
    {
        const auto n = st.tour.size();
        const auto first = st.tour[mv.first_edge];
        const auto first_next = st.tour[(mv.first_edge + 1) % n];
        const auto second = st.tour[mv.second_edge];
        const auto second_next = st.tour[(mv.second_edge + 1) % n];

        const auto removed = in.Distance(first, first_next) + in.Distance(second, second_next);
        const auto added = in.Distance(first, second) + in.Distance(first_next, second_next);
        return added - removed;
    }
};

class TwoOptNeighborhoodExplorer
    : public NeighborhoodExplorer<Input, Tour, TwoOpt, CostStructure>
{
public:
    TwoOptNeighborhoodExplorer(const Input& in, TspSolutionManager& sm)
        : NeighborhoodExplorer<Input, Tour, TwoOpt, CostStructure>(in, sm, "2-opt") {}

    bool FeasibleMove(const Tour& st, const TwoOpt& mv) const override
    {
        return ValidEdgePair(st.tour.size(), mv.first_edge, mv.second_edge);
    }

    // Uniform over the valid pairs in expected O(1), as the EasyLocal 4
    // random_move: two edges drawn independently, ordered, and drawn again
    // while they do not form a valid move.
    void RandomMove(const Tour& st, TwoOpt& mv) const override
    {
        const auto n = st.tour.size();
        if (MoveCount(n) == 0)
            throw EmptyNeighborhood();
        while (true)
        {
            auto first_edge = Random::Uniform<std::size_t>(0, n - 1);
            auto second_edge = Random::Uniform<std::size_t>(0, n - 1);
            if (second_edge < first_edge)
                std::swap(first_edge, second_edge);
            if (ValidEdgePair(n, first_edge, second_edge))
            {
                mv = TwoOpt{first_edge, second_edge};
                return;
            }
        }
    }

    void FirstMove(const Tour& st, TwoOpt& mv) const override
    {
        if (!FindFrom(st.tour.size(), 0, 1, mv))
            throw EmptyNeighborhood();
    }

    bool NextMove(const Tour& st, TwoOpt& mv) const override
    {
        return FindFrom(st.tour.size(), mv.first_edge, mv.second_edge + 1, mv);
    }

    void MakeMove(Tour& st, const TwoOpt& mv) const override
    {
        std::reverse(st.tour.begin() + static_cast<std::ptrdiff_t>(mv.first_edge + 1),
                     st.tour.begin() + static_cast<std::ptrdiff_t>(mv.second_edge + 1));
    }

private:
    static bool ValidEdgePair(std::size_t n, std::size_t first_edge, std::size_t second_edge)
    {
        return first_edge < second_edge && second_edge < n &&
               second_edge != first_edge + 1 &&
               !(first_edge == 0 && second_edge + 1 == n);
    }

    static std::size_t MoveCount(std::size_t n)
    {
        return n >= 4 ? n * (n - 3) / 2 : 0;
    }

    static bool FindFrom(std::size_t n, std::size_t initial_first_edge,
                         std::size_t initial_second_edge, TwoOpt& mv)
    {
        for (auto first_edge = initial_first_edge; first_edge < n; ++first_edge)
        {
            const auto second_begin =
                first_edge == initial_first_edge ? initial_second_edge : first_edge + 1;
            for (auto second_edge = second_begin; second_edge < n; ++second_edge)
                if (ValidEdgePair(n, first_edge, second_edge))
                {
                    mv = TwoOpt{first_edge, second_edge};
                    return true;
                }
        }
        return false;
    }
};

// The swap neighborhood of the EasyLocal 4 example (examples/tsp/
// swap_move.hpp): exchange the cities visited at two positions of the tour.
class SwapCities
{
public:
    std::size_t first_position = 0, second_position = 0;
};

inline bool operator==(const SwapCities& a, const SwapCities& b)
{
    return a.first_position == b.first_position &&
           a.second_position == b.second_position;
}
inline bool operator!=(const SwapCities& a, const SwapCities& b) { return !(a == b); }
inline bool operator<(const SwapCities& a, const SwapCities& b)
{
    return a.first_position < b.first_position ||
           (a.first_position == b.first_position && a.second_position < b.second_position);
}
inline std::ostream& operator<<(std::ostream& os, const SwapCities& mv)
{
    return os << "position " << mv.first_position << " <-> position " << mv.second_position;
}

class SwapTourLengthDelta : public DeltaCostComponent<Input, Tour, SwapCities, double>
{
public:
    SwapTourLengthDelta(const Input& in, TourLength& cc)
        : DeltaCostComponent<Input, Tour, SwapCities, double>(in, cc, "SwapTourLengthDelta") {}

    // The at most four edges the swap replaces, each counted once.
    double ComputeDeltaCost(const Tour& st, const SwapCities& mv) const override
    {
        const auto n = st.tour.size();
        const std::size_t affected[4] = {
            (mv.first_position + n - 1) % n,
            mv.first_position,
            (mv.second_position + n - 1) % n,
            mv.second_position,
        };

        double removed = 0.0, added = 0.0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            bool duplicate = false;
            for (std::size_t j = 0; j < i; ++j)
                duplicate = duplicate || affected[j] == affected[i];
            if (duplicate)
                continue;

            const auto edge = affected[i];
            const auto next = (edge + 1) % n;
            removed += in.Distance(st.tour[edge], st.tour[next]);
            added += in.Distance(CityAfterSwap(st, mv, edge), CityAfterSwap(st, mv, next));
        }
        return added - removed;
    }

private:
    static std::size_t CityAfterSwap(const Tour& st, const SwapCities& mv, std::size_t position)
    {
        if (position == mv.first_position)
            return st.tour[mv.second_position];
        if (position == mv.second_position)
            return st.tour[mv.first_position];
        return st.tour[position];
    }
};

class SwapCitiesNeighborhoodExplorer
    : public NeighborhoodExplorer<Input, Tour, SwapCities, CostStructure>
{
public:
    SwapCitiesNeighborhoodExplorer(const Input& in, TspSolutionManager& sm)
        : NeighborhoodExplorer<Input, Tour, SwapCities, CostStructure>(in, sm, "swap") {}

    bool FeasibleMove(const Tour& st, const SwapCities& mv) const override
    {
        return mv.first_position < mv.second_position && mv.second_position < st.tour.size();
    }

    // Uniform over the pairs in O(1), as the EasyLocal 4 random_move: a
    // position, then another among the rest, ordered.
    void RandomMove(const Tour& st, SwapCities& mv) const override
    {
        const auto n = st.tour.size();
        if (n < 2)
            throw EmptyNeighborhood();
        const auto first = Random::Uniform<std::size_t>(0, n - 1);
        auto second = Random::Uniform<std::size_t>(0, n - 2);
        if (second >= first)
            ++second;
        mv = SwapCities{std::min(first, second), std::max(first, second)};
    }

    // Every pair of positions i < j, in lexicographic order.
    void FirstMove(const Tour& st, SwapCities& mv) const override
    {
        if (st.tour.size() < 2)
            throw EmptyNeighborhood();
        mv = SwapCities{0, 1};
    }

    bool NextMove(const Tour& st, SwapCities& mv) const override
    {
        const auto n = st.tour.size();
        if (mv.second_position + 1 < n)
        {
            mv = SwapCities{mv.first_position, mv.second_position + 1};
            return true;
        }
        if (mv.first_position + 2 < n)
        {
            mv = SwapCities{mv.first_position + 1, mv.first_position + 2};
            return true;
        }
        return false;
    }

    void MakeMove(Tour& st, const SwapCities& mv) const override
    {
        std::swap(st.tour[mv.first_position], st.tour[mv.second_position]);
    }
};

} // namespace tsp
