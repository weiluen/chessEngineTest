#include "chess/bitboard.hpp"
#include "chess/evaluation.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"
#include "chess/transposition_table.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct BenchCase {
    std::string name;
    std::string fen;
    int depth;
};

const std::vector<BenchCase> kBenchCases = {
    {"Start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5},
    {"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4},
    {"Midgame", "2r1r1k1/pp2qppp/2npbn2/2p5/2P5/2N1PN2/PPQ2PPP/2KR1B1R w - - 0 1", 4},
    {"Endgame", "8/6pk/8/2KP4/4P3/7p/6b1/8 w - - 0 1", 6}
};

std::pair<std::uint64_t, long long> run_case(const BenchCase& test) {
    using namespace chess;
    Position pos;
    pos.set_fen(test.fen);

    TranspositionTable tt(128);
    Search search(tt);

    SearchLimits limits;
    limits.depth = test.depth;

    auto start = std::chrono::steady_clock::now();
    SearchResult result = search.iterative_deepening(pos, limits);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (elapsed_ms <= 0) elapsed_ms = 1;
    std::uint64_t nodes = result.stats.nodes + result.stats.qnodes;
    std::uint64_t nps = static_cast<std::uint64_t>((nodes * 1000ULL) / elapsed_ms);

    std::cout << std::left << std::setw(10) << test.name
              << " depth " << std::setw(2) << result.depth_reached
              << " nodes " << std::setw(12) << nodes
              << " nps " << std::setw(10) << nps
              << " time(ms) " << elapsed_ms
              << " best " << to_uci(result.best_move)
              << '\n';

    return {nodes, elapsed_ms};
}

}  // namespace

int main() {
    using namespace chess;
    init_attack_tables();
    init_evaluation();

    std::cout << "HyperSearch bench (depth-limited)\n";
    std::uint64_t total_nodes = 0;
    long long total_ms = 0;

    for (const auto& test : kBenchCases) {
        auto [nodes, time_ms] = run_case(test);
        total_nodes += nodes;
        total_ms += time_ms;
    }

    if (total_ms <= 0) total_ms = 1;
    std::uint64_t total_nps = static_cast<std::uint64_t>((total_nodes * 1000ULL) / total_ms);
    std::cout << "Total nodes " << total_nodes
              << " total time(ms) " << total_ms
              << " aggregate nps " << total_nps
              << '\n';
    return 0;
}
