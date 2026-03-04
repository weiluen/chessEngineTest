#include "chess/move.hpp"

#include <string>

namespace chess {

namespace {

char promotion_char(Piece piece) {
    switch (piece) {
        case Piece::Queen: return 'q';
        case Piece::Rook: return 'r';
        case Piece::Bishop: return 'b';
        case Piece::Knight: return 'n';
        default: return 'q';
    }
}

}  // namespace

std::string to_uci(const Move& move) {
    if (move.is_null()) {
        return "0000";
    }
    auto square_to_str = [](std::uint16_t sq) {
        std::string s;
        s.push_back('a' + static_cast<char>(sq % 8));
        s.push_back('1' + static_cast<char>(sq / 8));
        return s;
    };

    std::string uci = square_to_str(move.from) + square_to_str(move.to);
    if (move.flags & MoveFlagPromotion) {
        uci.push_back(promotion_char(move.promotion));
    }
    return uci;
}

}  // namespace chess
