#include "chess/bitboard.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"
#include "chess/transposition_table.hpp"
#include "chess/uci.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    using namespace chess;

    init_attack_tables();

    bool cli_mode = (argc > 1 && std::string(argv[1]) == "--cli");

    if (!cli_mode) {
        uci::loop();
        return 0;
    }

    try {
        std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        SearchLimits limits;
        limits.depth = 5;

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--fen" || arg == "-f") && i + 1 < argc) {
                fen = argv[++i];
            } else if ((arg == "--depth" || arg == "-d") && i + 1 < argc) {
                limits.depth = std::stoi(argv[++i]);
            } else if (arg == "--movetime" && i + 1 < argc) {
                limits.time_ms = static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--nodes" && i + 1 < argc) {
                limits.nodes = static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--wtime" && i + 1 < argc) {
                limits.time_left[static_cast<int>(Color::White)] =
                    static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--btime" && i + 1 < argc) {
                limits.time_left[static_cast<int>(Color::Black)] =
                    static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--winc" && i + 1 < argc) {
                limits.increment[static_cast<int>(Color::White)] =
                    static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--binc" && i + 1 < argc) {
                limits.increment[static_cast<int>(Color::Black)] =
                    static_cast<std::uint64_t>(std::stoll(argv[++i]));
            } else if (arg == "--movestogo" && i + 1 < argc) {
                limits.moves_to_go = std::stoi(argv[++i]);
            } else if (arg == "--infinite") {
                limits.infinite = true;
            }
        }

        Position pos;
        pos.set_fen(fen);

        TranspositionTable tt(64);
        Search search(tt);
        auto result = search.iterative_deepening(pos, limits);

        std::cout << "bestmove " << to_uci(result.best_move);
        if (std::abs(result.score) > CheckmateThreshold) {
            int mate_in = (CheckmateScore - std::abs(result.score)) / 2;
            if (mate_in < 0) mate_in = 0;
            std::cout << " mate " << (result.score > 0 ? mate_in : -mate_in);
        } else {
            std::cout << " cp " << result.score;
        }
        std::cout << " depth " << result.depth_reached
                  << " nodes " << result.stats.nodes
                  << " qnodes " << result.stats.qnodes
                  << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
