/**
 * Move encoding helpers for the chess engine.
 */
#pragma once

#include "types.hpp"

#include <string>

namespace chess {

enum : std::uint8_t {
    MoveFlagQuiet = 0,
    MoveFlagCapture = 1 << 0,
    MoveFlagDoublePawn = 1 << 1,
    MoveFlagEnPassant = 1 << 2,
    MoveFlagCastling = 1 << 3,
    MoveFlagPromotion = 1 << 4,
    MoveFlagCheck = 1 << 5
};

[[nodiscard]] constexpr Move make_null_move() noexcept {
    return Move{
        .from = 0,
        .to = 0,
        .piece = Piece::None,
        .capture = Piece::None,
        .promotion = Piece::None,
        .flags = 0
    };
}

[[nodiscard]] std::string to_uci(const Move& move);

}  // namespace chess
