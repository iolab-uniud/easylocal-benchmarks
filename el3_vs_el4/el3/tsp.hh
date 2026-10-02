// EasyLocal 3 port of the EasyLocal 4 TSP example (examples/tsp): 2-opt
// neighborhood, tour length with the closing edge, O(1) 2-opt delta.
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
        std::shuffle(st.tour.begin(), st.tour.end(), Random::GetGenerator());
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

    // Uniform over the valid pairs: a rank decoded by the same linear scan
    // as the EasyLocal 4 move_at_rank.
    void RandomMove(const Tour& st, TwoOpt& mv) const override
    {
        const auto count = MoveCount(st.tour.size());
        if (count == 0)
            throw EmptyNeighborhood();
        mv = MoveAtRank(st.tour.size(), Random::Uniform<std::size_t>(0, count - 1));
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

    static TwoOpt MoveAtRank(std::size_t n, std::size_t rank)
    {
        std::size_t current_rank = 0;
        for (std::size_t first_edge = 0; first_edge < n; ++first_edge)
            for (std::size_t second_edge = first_edge + 1; second_edge < n; ++second_edge)
            {
                if (!ValidEdgePair(n, first_edge, second_edge))
                    continue;
                if (current_rank == rank)
                    return TwoOpt{first_edge, second_edge};
                ++current_rank;
            }
        throw std::logic_error("2-opt move rank must decode to a valid move");
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

} // namespace tsp
