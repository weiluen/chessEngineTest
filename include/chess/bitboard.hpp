/**
 * Bitboard utilities and precomputed attack tables.
 */
#pragma once

#include "types.hpp"

#include <array>
#include <cstddef>

namespace chess {

enum Square : std::uint8_t {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SquareNone
};

constexpr Bitboard SquareBB[64] = {
    1ULL << 0,  1ULL << 1,  1ULL << 2,  1ULL << 3,  1ULL << 4,  1ULL << 5,  1ULL << 6,  1ULL << 7,
    1ULL << 8,  1ULL << 9,  1ULL << 10, 1ULL << 11, 1ULL << 12, 1ULL << 13, 1ULL << 14, 1ULL << 15,
    1ULL << 16, 1ULL << 17, 1ULL << 18, 1ULL << 19, 1ULL << 20, 1ULL << 21, 1ULL << 22, 1ULL << 23,
    1ULL << 24, 1ULL << 25, 1ULL << 26, 1ULL << 27, 1ULL << 28, 1ULL << 29, 1ULL << 30, 1ULL << 31,
    1ULL << 32, 1ULL << 33, 1ULL << 34, 1ULL << 35, 1ULL << 36, 1ULL << 37, 1ULL << 38, 1ULL << 39,
    1ULL << 40, 1ULL << 41, 1ULL << 42, 1ULL << 43, 1ULL << 44, 1ULL << 45, 1ULL << 46, 1ULL << 47,
    1ULL << 48, 1ULL << 49, 1ULL << 50, 1ULL << 51, 1ULL << 52, 1ULL << 53, 1ULL << 54, 1ULL << 55,
    1ULL << 56, 1ULL << 57, 1ULL << 58, 1ULL << 59, 1ULL << 60, 1ULL << 61, 1ULL << 62, 1ULL << 63
};

[[nodiscard]] constexpr Bitboard bit(Square sq) noexcept {
    return SquareBB[static_cast<int>(sq)];
}

[[nodiscard]] constexpr int lsb(Bitboard b) noexcept {
    return b ? __builtin_ctzll(b) : 0;
}

[[nodiscard]] constexpr int popcount(Bitboard b) noexcept {
    return __builtin_popcountll(b);
}

void init_attack_tables();

[[nodiscard]] Bitboard knight_attacks(Square sq) noexcept;
[[nodiscard]] Bitboard king_attacks(Square sq) noexcept;
[[nodiscard]] Bitboard pawn_attacks(Color side, Square sq) noexcept;
[[nodiscard]] Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept;
[[nodiscard]] Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept;

}  // namespace chess
