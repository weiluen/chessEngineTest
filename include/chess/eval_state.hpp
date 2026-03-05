/**
 * Evaluation state maintained incrementally alongside the position.
 */
#pragma once

#include "bitboard.hpp"
#include "types.hpp"

#include <array>

namespace chess {

struct EvalState {
    std::array<int, 2> material_mg{0, 0};
    std::array<int, 2> material_eg{0, 0};
    std::array<int, 2> psq_mg{0, 0};
    std::array<int, 2> psq_eg{0, 0};
    std::array<int, 2> pawn_mg{0, 0};
    std::array<int, 2> pawn_eg{0, 0};
    std::array<int, 2> mobility_mg{0, 0};
    std::array<int, 2> mobility_eg{0, 0};
    std::array<int, 2> king_safety{0, 0};  // reserved
    int passed_extras_mg = 0;
    int passed_extras_eg = 0;
    Key pawn_key = 0;
    bool pawn_dirty = true;
    bool mobility_dirty = true;
    bool king_dirty = true;
    bool passed_extras_dirty = true;
    int phase = 0;
};

}  // namespace chess
