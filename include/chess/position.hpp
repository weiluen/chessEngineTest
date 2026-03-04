/**
 * Position representation, move generation and make/unmake facilities.
 */
#pragma once

#include "bitboard.hpp"
#include "move.hpp"
#include "types.hpp"

#include <array>
#include <string>
#include <vector>

namespace chess {

struct StateInfo {
    Key zobrist = 0;
    Move last_move = make_null_move();
    Piece captured = Piece::None;
    int fifty_move_counter = 0;
    int ply_from_null = 0;
    Square ep_square = SquareNone;
    std::uint8_t castling_rights = 0;
};

class Position {
public:
    Position();

    void set_fen(const std::string& fen);

    [[nodiscard]] std::string fen() const;

    [[nodiscard]] Color side_to_move() const noexcept { return side_to_move_; }
    [[nodiscard]] int ply() const noexcept { return ply_; }

    bool make_move(Move move);
    void unmake_move();

    bool make_null_move();
    void unmake_null_move();

    [[nodiscard]] std::vector<Move> generate_legal_moves() const;
    [[nodiscard]] bool in_check(Color side) const;
    [[nodiscard]] int material_balance() const;
    [[nodiscard]] Bitboard pieces(Color color, Piece piece) const noexcept {
        return pieces_[static_cast<int>(color)][static_cast<int>(piece)];
    }

    [[nodiscard]] Bitboard occupancy() const noexcept { return occupancy_[0] | occupancy_[1]; }
    [[nodiscard]] Bitboard occupancy(Color side) const noexcept { return occupancy_[static_cast<int>(side)]; }
    [[nodiscard]] Square ep_square() const noexcept { return state_stack_[ply_].ep_square; }
    [[nodiscard]] std::uint8_t castling_rights() const noexcept { return state_stack_[ply_].castling_rights; }

    [[nodiscard]] Key zobrist() const noexcept { return state_stack_[ply_].zobrist; }

private:
    void clear();
    void push_state();
    void pop_state();
    void add_piece(Color color, Piece piece, Square sq);
    void remove_piece(Color color, Piece piece, Square sq);
    [[nodiscard]] Piece piece_on(Color color, Square sq) const;
    [[nodiscard]] bool is_square_attacked(Color attacker, Square sq) const;
    [[nodiscard]] Key compute_zobrist() const;

    std::array<Bitboard, 6> pieces_[2]{};
    std::array<Bitboard, 2> occupancy_{};
    Color side_to_move_ = Color::White;
    int ply_ = 0;
    std::array<StateInfo, MaxPly> state_stack_{};
};

}  // namespace chess
