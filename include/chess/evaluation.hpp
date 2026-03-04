#pragma once

#include "bitboard.hpp"
#include "eval_state.hpp"
#include "types.hpp"

namespace chess {

class Position;

void init_evaluation();
EvalState build_eval_state(const Position& pos);
void eval_on_piece_add(EvalState& state, Color color, Piece piece, Square sq);
void eval_on_piece_remove(EvalState& state, Color color, Piece piece, Square sq);
int evaluate(const Position& pos, EvalState& state);

}  // namespace chess
