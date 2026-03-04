#include "chess/evaluation.hpp"

#include <algorithm>
#include <array>

namespace chess {

namespace {

using Table = std::array<int, 64>;

constexpr Table PawnMG = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5,  5,  5, -10, -10,  5,  5,  5,
     5, 10, 12,  15,  15, 12, 10,  5,
     0,  5, 10,  20,  20, 10,  5,  0,
     0,  5, 10,  18,  18, 10,  5,  0,
     5,  5,  5,   0,   0,  5,  5,  5,
     5,  5,  5, -10, -10,  5,  5,  5,
     0,  0,  0,   0,   0,  0,  0,  0
};

constexpr Table PawnEG = {
     0,  0,  0,   0,   0,  0,  0,  0,
     5,  5,  5,  -5,  -5,  5,  5,  5,
    10, 10, 12,  12,  12, 12, 10, 10,
    12, 12, 18,  20,  20, 18, 12, 12,
    15, 15, 20,  25,  25, 20, 15, 15,
    20, 20, 25,  30,  30, 25, 20, 20,
    25, 25, 30,  35,  35, 30, 25, 25,
    30, 30, 35,  40,  40, 35, 30, 30
};

constexpr Table KnightMG = {
   -50, -40, -30, -30, -30, -30, -40, -50,
   -40, -20,   0,   5,   5,   0, -20, -40,
   -30,   5,  15,  20,  20,  15,   5, -30,
   -20,  10,  20,  25,  25,  20,  10, -20,
   -20,  10,  20,  25,  25,  20,  10, -20,
   -30,   5,  15,  20,  20,  15,   5, -30,
   -40, -20,   0,   5,   5,   0, -20, -40,
   -50, -40, -30, -30, -30, -30, -40, -50
};

constexpr Table KnightEG = KnightMG;

constexpr Table BishopMG = {
   -18, -12, -10,  -8,  -8, -10, -12, -18,
   -12,   0,   5,  10,  10,   5,   0, -12,
   -10,   5,  12,  15,  15,  12,   5, -10,
    -8,  10,  15,  20,  20,  15,  10,  -8,
    -8,   8,  15,  22,  22,  15,   8,  -8,
   -10,   5,  12,  15,  15,  12,   5, -10,
   -12,   0,   5,  10,  10,   5,   0, -12,
   -18, -12, -10,  -8,  -8, -10, -12, -18
};

constexpr Table BishopEG = {
   -16, -10,  -8,  -6,  -6,  -8, -10, -16,
   -10,   2,   5,   8,   8,   5,   2, -10,
    -8,   5,  10,  12,  12,  10,   5,  -8,
    -6,   8,  12,  18,  18,  12,   8,  -6,
    -6,   8,  12,  18,  18,  12,   8,  -6,
    -8,   5,  10,  12,  12,  10,   5,  -8,
   -10,   2,   5,   8,   8,   5,   2, -10,
   -16, -10,  -8,  -6,  -6,  -8, -10, -16
};

constexpr Table RookMG = {
     0,   0,   5,  10,  10,   5,   0,   0,
    -5,   0,   0,   5,   5,   0,   0,  -5,
    -5,   0,   5,  10,  10,   5,   0,  -5,
    -5,   0,   5,  12,  12,   5,   0,  -5,
    -5,   0,   5,  12,  12,   5,   0,  -5,
    -5,   0,   5,  10,  10,   5,   0,  -5,
     5,  10,  10,  15,  15,  10,  10,   5,
     0,   0,   5,  10,  10,   5,   0,   0
};

constexpr Table RookEG = {
     0,   0,   5,   8,   8,   5,   0,   0,
     2,   4,   8,  10,  10,   8,   4,   2,
     4,   8,  10,  12,  12,  10,   8,   4,
     6,  10,  12,  15,  15,  12,  10,   6,
     6,  10,  12,  15,  15,  12,  10,   6,
     4,   8,  10,  12,  12,  10,   8,   4,
     2,   4,   8,  10,  10,   8,   4,   2,
     0,   0,   5,   8,   8,   5,   0,   0
};

constexpr Table QueenMG = {
   -12, -10,  -8,  -6,  -6,  -8, -10, -12,
   -10,  -5,   0,   0,   0,   0,  -5, -10,
    -8,   0,   5,   8,   8,   5,   0,  -8,
    -6,   0,   8,  10,  10,   8,   0,  -6,
    -6,   0,   8,  10,  10,   8,   0,  -6,
    -8,   0,   5,   8,   8,   5,   0,  -8,
   -10,  -5,   0,   0,   0,   0,  -5, -10,
   -12, -10,  -8,  -6,  -6,  -8, -10, -12
};

constexpr Table QueenEG = {
   -10,  -8,  -6,  -4,  -4,  -6,  -8, -10,
    -8,  -4,   0,   2,   2,   0,  -4,  -8,
    -6,   0,   4,   6,   6,   4,   0,  -6,
    -4,   2,   6,   8,   8,   6,   2,  -4,
    -4,   2,   6,   8,   8,   6,   2,  -4,
    -6,   0,   4,   6,   6,   4,   0,  -6,
    -8,  -4,   0,   2,   2,   0,  -4,  -8,
   -10,  -8,  -6,  -4,  -4,  -6,  -8, -10
};

constexpr Table KingMG = {
    40,  50,  20,   0,   0,  20,  50,  40,
    30,  40,  10,   0,   0,  10,  40,  30,
    10,  20,   0, -10, -10,   0,  20,  10,
     0,   0, -10, -25, -25, -10,   0,   0,
     0,   0, -10, -25, -25, -10,   0,   0,
    10, -10, -20, -30, -30, -20, -10,  10,
    30, -20, -30, -40, -40, -30, -20,  30,
    20,  10, -20, -30, -30, -20,  10,  20
};

constexpr Table KingEG = {
   -45, -40, -30, -20, -20, -30, -40, -45,
   -35, -20, -10,  -5,  -5, -10, -20, -35,
   -25, -10,  10,  15,  15,  10, -10, -25,
   -15,  -5,  15,  25,  25,  15,  -5, -15,
   -15,  -5,  15,  25,  25,  15,  -5, -15,
   -25, -10,  10,  15,  15,  10, -10, -25,
   -35, -20, -10,  -5,  -5, -10, -20, -35,
   -45, -40, -30, -20, -20, -30, -40, -45
};

constexpr std::array<Table, 6> PST_MG = {
    PawnMG, KnightMG, BishopMG, RookMG, QueenMG, KingMG
};

constexpr std::array<Table, 6> PST_EG = {
    PawnEG, KnightEG, BishopEG, RookEG, QueenEG, KingEG
};

constexpr int PieceValueMG[6] = {82, 337, 365, 477, 1025, 0};
constexpr int PieceValueEG[6] = {94, 281, 297, 512, 936, 0};
constexpr int PiecePhase[6] = {0, 1, 1, 2, 4, 0};
constexpr int PhaseTotal = 24;

inline int mirror_square(int sq) {
    return sq ^ 56;
}

inline Square pop_lsb(Bitboard& bb) {
    Square sq = static_cast<Square>(lsb(bb));
    bb &= bb - 1;
    return sq;
}

}  // namespace

int evaluate(const Position& pos) {
    int mg_score = 0;
    int eg_score = 0;
    int phase = 0;

    for (int color = 0; color < 2; ++color) {
        const Color c = static_cast<Color>(color);
        int sign = (c == Color::White) ? 1 : -1;
        for (int piece = 0; piece < 6; ++piece) {
            Bitboard bb = pos.pieces(c, static_cast<Piece>(piece));
            int count = popcount(bb);
            if (piece != static_cast<int>(Piece::Pawn) && piece != static_cast<int>(Piece::King)) {
                phase += PiecePhase[piece] * count;
            }
            while (bb) {
                Square sq = pop_lsb(bb);
                int idx = static_cast<int>(sq);
                if (c == Color::Black) {
                    idx = mirror_square(idx);
                }
                mg_score += sign * (PieceValueMG[piece] + PST_MG[piece][idx]);
                eg_score += sign * (PieceValueEG[piece] + PST_EG[piece][idx]);
            }
        }
    }

    phase = std::clamp(phase, 0, PhaseTotal);
    int blended = (mg_score * phase + eg_score * (PhaseTotal - phase)) / PhaseTotal;
    const int tempo = 10;
    blended += tempo * (pos.side_to_move() == Color::White ? 1 : -1);
    return pos.side_to_move() == Color::White ? blended : -blended;
}

}  // namespace chess
