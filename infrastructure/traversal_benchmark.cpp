#include "assignment_variants.hpp"
#include "tsp_union_variants.hpp"
#include "tsp_variants.hpp"

#include <easylocal/helpers/neighborhood_explorer.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace bench = easylocal::benchmark::neighborhood_traversal;

namespace
{


template<std::ranges::input_range Range, class Encode>
[[nodiscard]]
auto encoded_moves(Range&& moves, Encode encode) -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> encoded;
    for (const auto& move : moves)
    {
        encoded.push_back(encode(move));
    }
    return encoded;
}

[[nodiscard]]
auto validate_assignment_variants() -> bool
{
    const assignment::AssignmentInstance instance{
        .demand = {2, 3, 5, 7},
        .capacity = {10, 10, 10},
    };
    const assignment::AssignmentSolution solution{
        .assignment = {0, 1, 2, 0},
    };
    const assignment::AssignmentSolutionManager manager{instance};

    const bench::assignment::CoroutineNeighborhoodExplorer coroutine{manager};
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    const bench::assignment::StdCoroutineNeighborhoodExplorer std_coroutine{manager};
#endif
    const bench::assignment::CursorNeighborhoodExplorer cursor{manager};
    const assignment::ReassignJobNeighborhoodExplorer example{manager};

    const auto encode = [](const assignment::ReassignJobMove& move) {
        return static_cast<std::uint64_t>(move.job) * 1024ULL +
               static_cast<std::uint64_t>(move.destination);
    };

    const auto expected = encoded_moves(easylocal::moves(cursor, solution), encode);
    if (expected != encoded_moves(example.moves(solution), encode) ||
        expected != encoded_moves(coroutine.moves(solution), encode))
    {
        return false;
    }
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    if (expected != encoded_moves(std_coroutine.moves(solution), encode))
    {
        return false;
    }
#endif
    return true;
}

// The union and the child-after-child enumeration give the same moves, in the
// same order: the benchmark compares the cost of the two, not their results.
[[nodiscard]]
auto validate_tsp_union_variants() -> bool
{
    constexpr std::size_t city_count = 6;
    const tsp::TspInstance instance{
        .city_count = city_count,
        .distances = std::vector<tsp::distance_type>(
            city_count * city_count,
            0.0),
    };
    const tsp::Tour solution{.tour = {0, 1, 2, 3, 4, 5}};
    const tsp::TspSolutionManager manager{instance};

    const auto neighborhood_union = bench::tsp_union::make_union(manager);
    const bench::tsp_union::ChildAfterChildExplorer child_after_child{manager};

    const auto encode = [](const bench::tsp_union::union_move_type& move) {
        return std::visit(
            [index = move.index()](const auto& tagged) -> std::uint64_t {
                if constexpr (tagged.index == 0)
                {
                    return static_cast<std::uint64_t>(tagged.value.first_edge) * 1024ULL
                        + static_cast<std::uint64_t>(tagged.value.second_edge);
                }
                else
                {
                    return 1'000'000ULL
                        + static_cast<std::uint64_t>(tagged.value.first_position)
                            * 1024ULL
                        + static_cast<std::uint64_t>(tagged.value.second_position);
                }
            },
            move);
    };

    return encoded_moves(easylocal::moves(neighborhood_union, solution), encode)
        == encoded_moves(easylocal::moves(child_after_child, solution), encode);
}

[[nodiscard]]
auto validate_tsp_variants() -> bool
{
    constexpr std::size_t city_count = 6;
    const tsp::TspInstance instance{
        .city_count = city_count,
        .distances = std::vector<tsp::distance_type>(
            city_count * city_count,
            0.0),
    };
    const tsp::Tour solution{
        .tour = {0, 1, 2, 3, 4, 5},
    };
    const tsp::TspSolutionManager manager{instance};

    const bench::tsp::CoroutineNeighborhoodExplorer coroutine{manager};
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    const bench::tsp::StdCoroutineNeighborhoodExplorer std_coroutine{manager};
#endif
    const bench::tsp::CursorNeighborhoodExplorer cursor{manager};
    const tsp::TwoOptNeighborhoodExplorer example{manager};

    const auto encode = [](const tsp::TwoOptMove& move) {
        return static_cast<std::uint64_t>(move.first_edge) * 1024ULL +
               static_cast<std::uint64_t>(move.second_edge);
    };

    const auto expected = encoded_moves(easylocal::moves(cursor, solution), encode);
    if (expected != encoded_moves(example.moves(solution), encode) ||
        expected != encoded_moves(coroutine.moves(solution), encode))
    {
        return false;
    }
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    if (expected != encoded_moves(std_coroutine.moves(solution), encode))
    {
        return false;
    }
#endif
    return true;
}

struct ScanResult
{
    std::uint64_t checksum{};
    std::size_t move_count{};
};

inline void observe(const std::uint64_t value) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    // Keep every produced move observable to the optimizer without adding a
    // volatile memory access to the measured hot loop. GCC and Clang may still
    // optimize the traversal machinery itself, which is exactly what this benchmark
    // wants to measure, but they cannot delete the per-move production entirely.
    __asm__ __volatile__("" : : "r"(value));
#else
    volatile auto sink = value;
    (void)sink;
#endif
}

template<class Explorer, class Solution, class Consumer>
[[nodiscard]]
auto scan_once(
    const Explorer& explorer,
    const Solution& solution,
    Consumer consume) -> ScanResult
{
    ScanResult result;

    for (const auto& move : easylocal::moves(explorer, solution))
    {
        const auto token = static_cast<std::uint64_t>(consume(move));
        observe(token);
        result.checksum += token;
        ++result.move_count;
    }

    observe(result.checksum);
    return result;
}

template<class Explorer, class Solution, class Consumer>
void run_case(
    const std::string_view domain,
    const std::string_view workload,
    const std::string_view variant,
    const Explorer& explorer,
    const Solution& solution,
    Consumer consume,
    const std::size_t target_moves,
    const std::size_t trials)
{
    const auto reference = scan_once(explorer, solution, consume);

    if (reference.move_count == 0)
    {
        std::cerr << domain << '/' << variant
                  << ": empty benchmark neighborhood\n";
        return;
    }

    const auto repetitions = std::max<std::size_t>(
        1,
        target_moves / reference.move_count);

    // Warm up code and data paths without including them in measurements.
    for (std::size_t warmup = 0; warmup < 3; ++warmup)
    {
        observe(scan_once(explorer, solution, consume).checksum);
    }

    for (std::size_t trial = 0; trial < trials; ++trial)
    {
        std::uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();

        for (std::size_t repetition = 0;
             repetition < repetitions;
             ++repetition)
        {
            checksum += scan_once(explorer, solution, consume).checksum;
        }

        const auto stop = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration<double, std::nano>(stop - start).count();
        const auto measured_moves = repetitions * reference.move_count;
        const auto ns_per_move =
            elapsed / static_cast<double>(measured_moves);

        observe(checksum);

        std::cout
            << domain << ','
            << workload << ','
            << variant << ','
            << reference.move_count << ','
            << trial << ','
            << repetitions << ','
            << measured_moves << ','
            << ns_per_move << ','
            << checksum << '\n';
    }
}

[[nodiscard]]
auto parse_positive(
    const int argc,
    char** argv,
    const int index,
    const std::size_t fallback,
    const std::string_view name) -> std::size_t
{
    if (argc <= index)
    {
        return fallback;
    }

    std::size_t result = 0;
    const std::string_view text{argv[index]};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), result);

    if (error != std::errc{} || end != text.data() + text.size() || result == 0)
    {
        std::cerr
            << "invalid " << name << " '" << text
            << "'; using " << fallback << '\n';
        return fallback;
    }

    return result;
}

[[nodiscard]]
auto parse_seed(
    const int argc,
    char** argv,
    const int index,
    const std::uint64_t fallback) -> std::uint64_t
{
    if (argc <= index)
    {
        return fallback;
    }

    std::uint64_t result = 0;
    const std::string_view text{argv[index]};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), result);

    if (error != std::errc{} || end != text.data() + text.size())
    {
        std::cerr
            << "invalid seed '" << text
            << "'; using " << fallback << '\n';
        return fallback;
    }

    return result;
}

void benchmark_assignment(
    const std::string_view scale,
    const std::size_t jobs,
    const std::size_t machines,
    const std::size_t target_moves,
    const std::size_t trials,
    const std::uint64_t seed)
{
    assignment::AssignmentInstance instance{
        .demand = std::vector<assignment::quantity_type>(jobs),
        .capacity = std::vector<assignment::quantity_type>(machines),
    };
    assignment::AssignmentSolution solution{
        .assignment = std::vector<assignment::machine_id>(jobs),
    };

    for (std::size_t job = 0; job < jobs; ++job)
    {
        instance.demand[job] = static_cast<assignment::quantity_type>(
            1 + ((job * 17ULL + seed) % 31ULL));
        solution.assignment[job] = job % machines;
    }

    for (std::size_t machine = 0; machine < machines; ++machine)
    {
        instance.capacity[machine] = static_cast<assignment::quantity_type>(
            10'000 + ((machine * 131ULL + seed) % 4096ULL));
    }

    const assignment::AssignmentSolutionManager manager{instance};
    const bench::assignment::CoroutineNeighborhoodExplorer coroutine_custom{manager};
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    const bench::assignment::StdCoroutineNeighborhoodExplorer coroutine_std{manager};
#endif
    const bench::assignment::CursorNeighborhoodExplorer cursor{manager};

    const auto traversal_consumer = [](const assignment::ReassignJobMove& move) {
        return static_cast<std::uint64_t>(move.job) * 1'000'003ULL +
               static_cast<std::uint64_t>(move.destination);
    };

    const auto light_consumer = [&](const assignment::ReassignJobMove& move) {
        const auto source = solution.assignment[move.job];
        auto token = traversal_consumer(move);
        token ^= (static_cast<std::uint64_t>(source) + 1ULL) * 65'537ULL;
        token ^= static_cast<std::uint64_t>(instance.demand[move.job]) *
                 1'000'000'007ULL;
        token ^= static_cast<std::uint64_t>(instance.capacity[move.destination]);
        return token;
    };

    const auto run_workload = [&](const std::string_view workload, auto consume) {
        const auto domain = std::string{"assignment-"} + std::string{scale};
        run_case(domain, workload, "coroutine-custom", coroutine_custom,
                 solution, consume, target_moves, trials);
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
        run_case(domain, workload, "coroutine-std", coroutine_std, solution,
                 consume, target_moves, trials);
#endif
        run_case(domain, workload, "cursor", cursor, solution, consume,
                 target_moves, trials);
    };

    run_workload("traversal", traversal_consumer);
    run_workload("light-consumer", light_consumer);
}

// The instance of the TSP domains: symmetric distances from the seed.
[[nodiscard]]
auto make_tsp_instance(const std::size_t city_count, const std::uint64_t seed)
    -> tsp::TspInstance
{
    tsp::TspInstance instance{
        .city_count = city_count,
        .distances = std::vector<tsp::distance_type>(
            city_count * city_count,
            0.0),
    };

    for (std::size_t first = 0; first < city_count; ++first)
    {
        for (std::size_t second = first + 1; second < city_count; ++second)
        {
            const auto raw =
                ((first + 1ULL) * 131'071ULL) ^
                ((second + 1ULL) * 524'287ULL) ^ seed;
            const auto distance =
                static_cast<tsp::distance_type>(1 + (raw % 65'535ULL)) / 16.0;
            instance.distances[first * city_count + second] = distance;
            instance.distances[second * city_count + first] = distance;
        }
    }

    return instance;
}

void benchmark_tsp(
    const std::string_view scale,
    const std::size_t city_count,
    const std::size_t target_moves,
    const std::size_t trials,
    const std::uint64_t seed)
{
    const auto instance = make_tsp_instance(city_count, seed);
    tsp::Tour solution{
        .tour = std::vector<tsp::city_id>(city_count),
    };

    for (std::size_t city = 0; city < city_count; ++city)
    {
        solution.tour[city] = city;
    }

    const tsp::TspSolutionManager manager{instance};
    const bench::tsp::CoroutineNeighborhoodExplorer coroutine_custom{manager};
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
    const bench::tsp::StdCoroutineNeighborhoodExplorer coroutine_std{manager};
#endif
    const bench::tsp::CursorNeighborhoodExplorer cursor{manager};

    const auto traversal_consumer = [](const tsp::TwoOptMove& move) {
        return static_cast<std::uint64_t>(move.first_edge) * 1'000'003ULL +
               static_cast<std::uint64_t>(move.second_edge);
    };

    const auto light_consumer = [&](const tsp::TwoOptMove& move) {
        const auto first = solution.tour[move.first_edge];
        const auto first_next =
            solution.tour[(move.first_edge + 1) % solution.tour.size()];
        const auto second = solution.tour[move.second_edge];
        const auto second_next =
            solution.tour[(move.second_edge + 1) % solution.tour.size()];

        auto token = traversal_consumer(move);
        token ^= std::bit_cast<std::uint64_t>(
            instance.distance(first, first_next));
        token ^= std::bit_cast<std::uint64_t>(
            instance.distance(second, second_next));
        token ^= std::bit_cast<std::uint64_t>(
            instance.distance(first, second));
        token ^= std::bit_cast<std::uint64_t>(
            instance.distance(first_next, second_next));
        return token;
    };

    const auto run_workload = [&](const std::string_view workload, auto consume) {
        const auto domain = std::string{"tsp-"} + std::string{scale};
        run_case(domain, workload, "coroutine-custom", coroutine_custom,
                 solution, consume, target_moves, trials);
#if EASYLOCAL_BENCHMARK_HAS_STD_GENERATOR
        run_case(domain, workload, "coroutine-std", coroutine_std, solution,
                 consume, target_moves, trials);
#endif
        run_case(domain, workload, "cursor", cursor, solution, consume,
                 target_moves, trials);
    };

    run_workload("traversal", traversal_consumer);
    run_workload("light-consumer", light_consumer);
}

// The union of the 2-opt and the swap neighborhoods: what its enumeration
// costs over enumerating the same moves child after child, which is the
// dispatch of the union's iterator on the variant that is its move.
void benchmark_tsp_union(
    const std::string_view scale,
    const std::size_t city_count,
    const std::size_t target_moves,
    const std::size_t trials,
    const std::uint64_t seed)
{
    const auto instance = make_tsp_instance(city_count, seed);
    ::tsp::Tour solution{.tour = std::vector<::tsp::city_id>(city_count)};
    for (std::size_t city = 0; city < city_count; ++city)
    {
        solution.tour[city] = city;
    }

    const ::tsp::TspSolutionManager manager{instance};
    namespace variants = bench::tsp_union;
    const auto neighborhood_union = variants::make_union(manager);
    const variants::ChildAfterChildExplorer child_after_child{manager};

    const auto traversal_consumer = [](const variants::union_move_type& move) {
        return std::visit(
            [](const auto& tagged) -> std::uint64_t {
                if constexpr (tagged.index == 0)
                {
                    return static_cast<std::uint64_t>(tagged.value.first_edge)
                               * 1'000'003ULL
                         + static_cast<std::uint64_t>(tagged.value.second_edge);
                }
                else
                {
                    return static_cast<std::uint64_t>(tagged.value.first_position)
                               * 1'000'033ULL
                         + static_cast<std::uint64_t>(tagged.value.second_position);
                }
            },
            move);
    };

    // A move evaluated as a delta would: four distances of the tour.
    const auto light_consumer = [&](const variants::union_move_type& move) {
        const auto size = solution.tour.size();
        const auto edges = std::visit(
            [](const auto& tagged) -> std::pair<std::size_t, std::size_t> {
                if constexpr (tagged.index == 0)
                {
                    return {tagged.value.first_edge, tagged.value.second_edge};
                }
                else
                {
                    return {tagged.value.first_position, tagged.value.second_position};
                }
            },
            move);
        const auto first = solution.tour[edges.first];
        const auto first_next = solution.tour[(edges.first + 1) % size];
        const auto second = solution.tour[edges.second];
        const auto second_next = solution.tour[(edges.second + 1) % size];

        auto token = traversal_consumer(move);
        token ^= std::bit_cast<std::uint64_t>(instance.distance(first, first_next));
        token ^= std::bit_cast<std::uint64_t>(instance.distance(second, second_next));
        token ^= std::bit_cast<std::uint64_t>(instance.distance(first, second));
        token ^= std::bit_cast<std::uint64_t>(
            instance.distance(first_next, second_next));
        return token;
    };

    const auto run_workload = [&](const std::string_view workload, auto consume) {
        const auto domain = std::string{"tsp-union-"} + std::string{scale};
        run_case(domain, workload, "union", neighborhood_union, solution, consume,
                 target_moves, trials);
        run_case(domain, workload, "child-after-child", child_after_child, solution,
                 consume, target_moves, trials);
    };

    run_workload("traversal", traversal_consumer);
    run_workload("light-consumer", light_consumer);
}

} // namespace

int main(const int argc, char** argv)
{
    constexpr std::size_t default_target_moves = 2'000'000;
    constexpr std::size_t default_trials = 3;
    constexpr std::uint64_t default_seed = 123'456'789ULL;

    const auto target_moves = parse_positive(
        argc, argv, 1, default_target_moves, "target move count");
    const auto trials = parse_positive(
        argc, argv, 2, default_trials, "trial count");
    const auto seed = parse_seed(argc, argv, 3, default_seed);

    std::cerr << "std::generator: "
              << (bench::has_std_generator ? "available" : "unavailable")
              << '\n';

    std::cout
        << "domain,workload,variant,neighborhood_size,trial,repetitions,"
           "measured_moves,ns_per_move,checksum\n";

    if (!validate_assignment_variants() || !validate_tsp_variants()
        || !validate_tsp_union_variants())
    {
        std::cerr << "neighborhood traversal variants disagree\n";
        return 1;
    }

    benchmark_assignment("medium", 256, 32, target_moves, trials, seed);
    benchmark_tsp("medium", 256, target_moves, trials, seed);
    benchmark_tsp_union("medium", 256, target_moves, trials, seed);

    return 0;
}
