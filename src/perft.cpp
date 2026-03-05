#include "chess/bitboard.hpp"
#include "chess/move.hpp"
#include "chess/position.hpp"

#include <iostream>
#include <string>

using namespace chess;

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) {
        return 1ULL;
    }
    MoveList moves = pos.generate_legal_moves();
    if (depth == 1) {
        return static_cast<std::uint64_t>(moves.size());
    }

    std::uint64_t nodes = 0;
    for (const Move& move : moves) {
        if (!pos.make_move(move)) {
            continue;
        }
        nodes += perft(pos, depth - 1);
        pos.unmake_move();
    }
    return nodes;
}

int main(int argc, char** argv) {
    init_attack_tables();

    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    int depth = 5;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--fen" || arg == "-f") && i + 1 < argc) {
            fen = argv[++i];
        } else if ((arg == "--depth" || arg == "-d") && i + 1 < argc) {
            depth = std::stoi(argv[++i]);
        }
    }

    try {
        Position pos;
        pos.set_fen(fen);
        std::uint64_t nodes = perft(pos, depth);
        std::cout << "Perft(" << depth << ") = " << nodes << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
