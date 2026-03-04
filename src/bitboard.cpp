#include "chess/bitboard.hpp"

#include <array>

namespace chess {

namespace {

std::array<Bitboard, 64> knight_table{};
std::array<Bitboard, 64> king_table{};
std::array<Bitboard, 64> pawn_table_white{};
std::array<Bitboard, 64> pawn_table_black{};

template <typename F>
constexpr Bitboard sliding_attacks(Square sq, Bitboard occupied, F step) noexcept {
    Bitboard attacks = 0ULL;
    int file = static_cast<int>(sq) % 8;
    int rank = static_cast<int>(sq) / 8;
    int df = 0;
    int dr = 0;
    step(df, dr);  // initialise direction increments
    int nf = file + df;
    int nr = rank + dr;
    while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
        Square target = static_cast<Square>(nr * 8 + nf);
        attacks |= bit(target);
        if (occupied & bit(target)) {
            break;
        }
        nf += df;
        nr += dr;
    }
    return attacks;
}

constexpr bool on_board(int file, int rank) noexcept {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

void init_knight_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        constexpr int offsets[8][2] = {
            {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
            {1, -2}, {2, -1}, {-1, -2}, {-2, -1}
        };
        for (auto [df, dr] : offsets) {
            int nf = file + df;
            int nr = rank + dr;
            if (on_board(nf, nr)) {
                mask |= bit(static_cast<Square>(nr * 8 + nf));
            }
        }
        knight_table[sq] = mask;
    }
}

void init_king_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) {
                    continue;
                }
                int nf = file + df;
                int nr = rank + dr;
                if (on_board(nf, nr)) {
                    mask |= bit(static_cast<Square>(nr * 8 + nf));
                }
            }
        }
        king_table[sq] = mask;
    }
}

void init_pawn_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask_white = 0ULL;
        Bitboard mask_black = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        int nr = rank + 1;
        if (on_board(file + 1, nr)) {
            mask_white |= bit(static_cast<Square>(nr * 8 + (file + 1)));
        }
        if (on_board(file - 1, nr)) {
            mask_white |= bit(static_cast<Square>(nr * 8 + (file - 1)));
        }
        nr = rank - 1;
        if (on_board(file + 1, nr)) {
            mask_black |= bit(static_cast<Square>(nr * 8 + (file + 1)));
        }
        if (on_board(file - 1, nr)) {
            mask_black |= bit(static_cast<Square>(nr * 8 + (file - 1)));
        }
        pawn_table_white[sq] = mask_white;
        pawn_table_black[sq] = mask_black;
    }
}

}  // namespace

void init_attack_tables() {
    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
}

Bitboard knight_attacks(Square sq) noexcept {
    return knight_table[static_cast<int>(sq)];
}

Bitboard king_attacks(Square sq) noexcept {
    return king_table[static_cast<int>(sq)];
}

Bitboard pawn_attacks(Color side, Square sq) noexcept {
    return side == Color::White
               ? pawn_table_white[static_cast<int>(sq)]
               : pawn_table_black[static_cast<int>(sq)];
}

Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
    Bitboard attacks = 0ULL;
    const int file = static_cast<int>(sq) % 8;
    const int rank = static_cast<int>(sq) / 8;
    constexpr int deltas[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (auto [df, dr] : deltas) {
        int nf = file + df;
        int nr = rank + dr;
        while (on_board(nf, nr)) {
            Square target = static_cast<Square>(nr * 8 + nf);
            attacks |= bit(target);
            if (occupied & bit(target)) {
                break;
            }
            nf += df;
            nr += dr;
        }
    }
    return attacks;
}

Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
    Bitboard attacks = 0ULL;
    const int file = static_cast<int>(sq) % 8;
    const int rank = static_cast<int>(sq) / 8;
    constexpr int deltas[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto [df, dr] : deltas) {
        int nf = file + df;
        int nr = rank + dr;
        while (on_board(nf, nr)) {
            Square target = static_cast<Square>(nr * 8 + nf);
            attacks |= bit(target);
            if (occupied & bit(target)) {
                break;
            }
            nf += df;
            nr += dr;
        }
    }
    return attacks;
}

}  // namespace chess
