/**
 * Common type aliases and compile-time constants for the chess engine core.
 */
#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace chess {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

constexpr int MaxPly = 128;
constexpr int MaxMoves = 256;
constexpr int InfiniteScore = 32000;
constexpr int CheckmateScore = 31000;
constexpr int CheckmateThreshold = CheckmateScore - MaxPly;
constexpr int DrawScore = 0;

enum class Color : std::uint8_t {
    White = 0,
    Black = 1,
    NoColor = 2
};

inline constexpr Color opposite(Color c) noexcept {
    return c == Color::White ? Color::Black :
           c == Color::Black ? Color::White :
           Color::NoColor;
}

enum class Piece : std::uint8_t {
    Pawn, Knight, Bishop, Rook, Queen, King, None
};

enum class Castling : std::uint8_t {
    WK = 1 << 0,
    WQ = 1 << 1,
    BK = 1 << 2,
    BQ = 1 << 3
};

struct Move {
    std::uint16_t from;
    std::uint16_t to;
    Piece piece;
    Piece capture;
    Piece promotion;
    std::uint8_t flags;

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return from == to && piece == Piece::None;
    }
};

struct MoveList {
    std::array<Move, MaxMoves> moves;
    int count = 0;

    void push_back(const Move& m) {
        if (count < MaxMoves) {
            moves[static_cast<std::size_t>(count++)] = m;
        }
    }
    Move& operator[](int i) { return moves[static_cast<std::size_t>(i)]; }
    const Move& operator[](int i) const { return moves[static_cast<std::size_t>(i)]; }
    int size() const { return count; }
    bool empty() const { return count == 0; }
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
    void clear() { count = 0; }
};

struct SearchLimits {
    int depth = 0;
    std::uint64_t nodes = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t time_ms = 0;
    std::uint64_t time_left[2] = {0, 0};
    std::uint64_t increment[2] = {0, 0};
    int moves_to_go = 0;
    bool infinite = false;
};

}  // namespace chess
