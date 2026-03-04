#include "chess/evaluation.hpp"

#include "chess/position.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

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

constexpr int DoubledPawnPenaltyMG = 12;
constexpr int DoubledPawnPenaltyEG = 8;
constexpr int IsolatedPawnPenaltyMG = 15;
constexpr int IsolatedPawnPenaltyEG = 12;
constexpr int BackwardPawnPenaltyMG = 6;
constexpr int BackwardPawnPenaltyEG = 6;
constexpr int PawnShieldBonusMG = 8;
constexpr int PawnStormPenaltyMG = 10;

constexpr int PassedBonusMG[8] = {0, 5, 10, 20, 35, 60, 90, 0};
constexpr int PassedBonusEG[8] = {0, 10, 20, 35, 60, 90, 140, 0};

constexpr int MobilityMG[6] = {0, 4, 5, 2, 1, 0};  // pawn .. king
constexpr int MobilityEG[6] = {0, 7, 7, 4, 2, 0};

inline int mirror_square(int sq) {
    return sq ^ 56;
}

inline int pst_index(Color color, Square sq) {
    int idx = static_cast<int>(sq);
    if (color == Color::Black) {
        idx = mirror_square(idx);
    }
    return idx;
}

inline Square pop_lsb(Bitboard& bb) {
    Square sq = static_cast<Square>(lsb(bb));
    bb &= bb - 1;
    return sq;
}

inline int file_of(Square sq) {
    return static_cast<int>(sq) % 8;
}

inline int rank_of(Square sq) {
    return static_cast<int>(sq) / 8;
}

inline Bitboard square_bb(Square sq) {
    return Bitboard(1) << static_cast<int>(sq);
}

struct PawnEntry {
    Key key = 0;
    std::array<int, 2> mg{};
    std::array<int, 2> eg{};
    bool valid = false;
};

class PawnTable {
public:
    explicit PawnTable(std::size_t size_mb = 4) {
        std::size_t entries = (size_mb * 1024ULL * 1024ULL) / sizeof(PawnEntry);
        if (entries == 0) entries = 1;
        std::size_t pow2 = 1;
        while (pow2 < entries) {
            pow2 <<= 1U;
        }
        table_.resize(pow2);
        mask_ = pow2 - 1;
    }

    bool probe(Key key, std::array<int, 2>& mg, std::array<int, 2>& eg) const {
        const PawnEntry& entry = table_[key & mask_];
        if (!entry.valid || entry.key != key) {
            return false;
        }
        mg = entry.mg;
        eg = entry.eg;
        return true;
    }

    void store(Key key, const std::array<int, 2>& mg, const std::array<int, 2>& eg) {
        PawnEntry& entry = table_[key & mask_];
        entry.key = key;
        entry.mg = mg;
        entry.eg = eg;
        entry.valid = true;
    }

private:
    std::vector<PawnEntry> table_;
    std::size_t mask_ = 0;
};

Key random_eval_key() {
    static Key seed = 0x9E3779B97F4A7C15ULL;
    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;
    return seed * 2685821657736338717ULL;
}

std::array<std::array<Key, 64>, 2> PawnZobrist{};
std::array<Bitboard, 8> FileMasks{};
std::array<Bitboard, 8> AdjacentFileMasks{};
std::array<std::array<Bitboard, 64>, 2> PassedMasks{};
std::array<std::array<Bitboard, 64>, 2> FrontSpans{};

PawnTable g_pawn_table;
bool eval_initialised = false;

inline void accumulate_piece(EvalState& state, Color color, Piece piece, Square sq, int multiplier) {
    if (piece == Piece::None) {
        return;
    }
    const int c = static_cast<int>(color);
    const int p = static_cast<int>(piece);
    const int idx = pst_index(color, sq);
    state.material_mg[c] += multiplier * PieceValueMG[p];
    state.material_eg[c] += multiplier * PieceValueEG[p];
    state.psq_mg[c] += multiplier * PST_MG[p][idx];
    state.psq_eg[c] += multiplier * PST_EG[p][idx];
    state.phase += multiplier * PiecePhase[p];
}

void init_tables() {
    for (int color = 0; color < 2; ++color) {
        for (int sq = 0; sq < 64; ++sq) {
            PawnZobrist[color][sq] = random_eval_key();
        }
    }
    for (int file = 0; file < 8; ++file) {
        Bitboard file_mask = 0;
        for (int rank = 0; rank < 8; ++rank) {
            file_mask |= Bitboard(1) << (rank * 8 + file);
        }
        FileMasks[file] = file_mask;
        Bitboard adj = 0;
        if (file > 0) adj |= FileMasks[file - 1];
        if (file < 7) adj |= FileMasks[file + 1];
        AdjacentFileMasks[file] = adj;
    }
    for (int sq = 0; sq < 64; ++sq) {
        for (int color = 0; color < 2; ++color) {
            Bitboard mask = 0;
            Bitboard front = 0;
            int file = file_of(static_cast<Square>(sq));
            int rank = rank_of(static_cast<Square>(sq));
            int dir = (color == static_cast<int>(Color::White)) ? 1 : -1;
            for (int r = rank + dir; r >= 0 && r < 8; r += dir) {
                front |= Bitboard(1) << (r * 8 + file);
                for (int df = -1; df <= 1; ++df) {
                    int nf = file + df;
                    if (nf < 0 || nf > 7) continue;
                    mask |= Bitboard(1) << (r * 8 + nf);
                }
            }
            PassedMasks[color][sq] = mask;
            FrontSpans[color][sq] = front;
        }
    }
}

inline bool is_doubled(Bitboard pawns, int file) {
    int count = popcount(pawns & FileMasks[file]);
    return count > 1;
}

inline bool is_isolated(Bitboard pawns, int file) {
    return (pawns & AdjacentFileMasks[file]) == 0;
}

inline bool is_backward(Color color, Square sq, Bitboard pawns, Bitboard enemy_pawns) {
    Bitboard file_front = FrontSpans[static_cast<int>(color)][static_cast<int>(sq)];
    int file = file_of(sq);
    Bitboard adjacent = AdjacentFileMasks[file] & file_front;
    Bitboard friendly_infront = pawns & adjacent;
    if (friendly_infront != 0) {
        return false;
    }
    return (enemy_pawns & adjacent) != 0;
}

inline bool is_passed(Color color, Square sq, Bitboard enemy_pawns) {
    return (enemy_pawns & PassedMasks[static_cast<int>(color)][static_cast<int>(sq)]) == 0;
}

Bitboard pawn_shield_mask(Color color, Square king_sq) {
    Bitboard mask = 0;
    int file = file_of(king_sq);
    int rank = rank_of(king_sq);
    if (color == Color::White) {
        if (rank > 6) return 0;
        for (int df = -1; df <= 1; ++df) {
            int nf = file + df;
            if (nf < 0 || nf > 7) continue;
            if (rank + 1 < 8) mask |= square_bb(static_cast<Square>((rank + 1) * 8 + nf));
            if (rank + 2 < 8) mask |= square_bb(static_cast<Square>((rank + 2) * 8 + nf));
        }
    } else {
        if (rank < 1) return 0;
        for (int df = -1; df <= 1; ++df) {
            int nf = file + df;
            if (nf < 0 || nf > 7) continue;
            if (rank - 1 >= 0) mask |= square_bb(static_cast<Square>((rank - 1) * 8 + nf));
            if (rank - 2 >= 0) mask |= square_bb(static_cast<Square>((rank - 2) * 8 + nf));
        }
    }
    return mask;
}

Bitboard pawn_storm_mask(Color color, Square king_sq) {
    Bitboard mask = 0;
    int file = file_of(king_sq);
    int rank = rank_of(king_sq);
    if (color == Color::White) {
        if (rank > 6) return 0;
        for (int df = -1; df <= 1; ++df) {
            int nf = file + df;
            if (nf < 0 || nf > 7) continue;
            if (rank + 3 < 8) mask |= square_bb(static_cast<Square>((rank + 3) * 8 + nf));
            if (rank + 4 < 8) mask |= square_bb(static_cast<Square>((rank + 4) * 8 + nf));
        }
    } else {
        if (rank < 1) return 0;
        for (int df = -1; df <= 1; ++df) {
            int nf = file + df;
            if (nf < 0 || nf > 7) continue;
            if (rank - 3 >= 0) mask |= square_bb(static_cast<Square>((rank - 3) * 8 + nf));
            if (rank - 4 >= 0) mask |= square_bb(static_cast<Square>((rank - 4) * 8 + nf));
        }
    }
    return mask;
}

void update_pawn_eval(const Position& pos, EvalState& state) {
    std::array<int, 2> mg{};
    std::array<int, 2> eg{};
    if (g_pawn_table.probe(state.pawn_key, mg, eg)) {
        state.pawn_mg = mg;
        state.pawn_eg = eg;
        state.pawn_dirty = false;
        return;
    }

    Bitboard white_pawns = pos.pieces(Color::White, Piece::Pawn);
    Bitboard black_pawns = pos.pieces(Color::Black, Piece::Pawn);

    auto evaluate_side = [&](Color color) {
        const int c = static_cast<int>(color);
        Bitboard pawns = (color == Color::White) ? white_pawns : black_pawns;
        Bitboard enemy_pawns = (color == Color::White) ? black_pawns : white_pawns;

        Bitboard pawns_iter = pawns;
        while (pawns_iter) {
            Square sq = pop_lsb(pawns_iter);
            int file = file_of(sq);
            int rank = rank_of(sq);
            if (is_doubled(pawns, file)) {
                mg[c] -= DoubledPawnPenaltyMG;
                eg[c] -= DoubledPawnPenaltyEG;
            }
            if (is_isolated(pawns, file)) {
                mg[c] -= IsolatedPawnPenaltyMG;
                eg[c] -= IsolatedPawnPenaltyEG;
            } else if (is_backward(color, sq, pawns, enemy_pawns)) {
                mg[c] -= BackwardPawnPenaltyMG;
                eg[c] -= BackwardPawnPenaltyEG;
            }
            if (is_passed(color, sq, enemy_pawns)) {
                int idx = (color == Color::White) ? rank : 7 - rank;
                mg[c] += PassedBonusMG[idx];
                eg[c] += PassedBonusEG[idx];
            }
        }
    };

    evaluate_side(Color::White);
    evaluate_side(Color::Black);

    auto add_king_safety = [&](Color color) {
        const int c = static_cast<int>(color);
        Bitboard king_bb = pos.pieces(color, Piece::King);
        if (!king_bb) return;
        Square king_sq = static_cast<Square>(lsb(king_bb));
        Bitboard pawns = (color == Color::White) ? white_pawns : black_pawns;
        Bitboard enemy_pawns = (color == Color::White) ? black_pawns : white_pawns;

        Bitboard shield_mask = pawn_shield_mask(color, king_sq);
        Bitboard shield_pawns = pawns & shield_mask;
        int shield = popcount(shield_pawns);
        mg[c] += shield * PawnShieldBonusMG;

        Bitboard storm_mask = pawn_storm_mask(color, king_sq);
        int storm = popcount(enemy_pawns & storm_mask);
        mg[c] -= storm * PawnStormPenaltyMG;
    };

    add_king_safety(Color::White);
    add_king_safety(Color::Black);

    state.pawn_mg = mg;
    state.pawn_eg = eg;
    state.pawn_dirty = false;
    g_pawn_table.store(state.pawn_key, mg, eg);
}

void update_mobility(const Position& pos, EvalState& state) {
    std::array<int, 2> mg{};
    std::array<int, 2> eg{};
    Bitboard occ_all = pos.occupancy();

    auto evaluate_piece = [&](Color color, Piece piece, Bitboard attacks) {
        const int c = static_cast<int>(color);
        Bitboard own_occ = pos.occupancy(color);
        Bitboard moves = attacks & ~own_occ;
        int count = popcount(moves);
        mg[c] += count * MobilityMG[static_cast<int>(piece)];
        eg[c] += count * MobilityEG[static_cast<int>(piece)];
    };

    for (int color = 0; color < 2; ++color) {
        Color side = static_cast<Color>(color);
        Bitboard knights = pos.pieces(side, Piece::Knight);
        Bitboard bishops = pos.pieces(side, Piece::Bishop);
        Bitboard rooks = pos.pieces(side, Piece::Rook);
        Bitboard queens = pos.pieces(side, Piece::Queen);

        Bitboard knights_iter = knights;
        while (knights_iter) {
            Square sq = pop_lsb(knights_iter);
            evaluate_piece(side, Piece::Knight, knight_attacks(sq));
        }

        Bitboard bishops_iter = bishops;
        while (bishops_iter) {
            Square sq = pop_lsb(bishops_iter);
            evaluate_piece(side, Piece::Bishop, bishop_attacks(sq, occ_all));
        }

        Bitboard rooks_iter = rooks;
        while (rooks_iter) {
            Square sq = pop_lsb(rooks_iter);
            evaluate_piece(side, Piece::Rook, rook_attacks(sq, occ_all));
        }

        Bitboard queens_iter = queens;
        while (queens_iter) {
            Square sq = pop_lsb(queens_iter);
            Bitboard attacks = bishop_attacks(sq, occ_all) | rook_attacks(sq, occ_all);
            evaluate_piece(side, Piece::Queen, attacks);
        }
    }

    state.mobility_mg = mg;
    state.mobility_eg = eg;
    state.mobility_dirty = false;
}

}  // namespace

void init_evaluation() {
    if (eval_initialised) {
        return;
    }
    init_tables();
    eval_initialised = true;
}

EvalState build_eval_state(const Position& pos) {
    init_evaluation();
    EvalState state{};
    for (int color = 0; color < 2; ++color) {
        const Color c = static_cast<Color>(color);
        for (int piece = 0; piece < 6; ++piece) {
            Bitboard bb = pos.pieces(c, static_cast<Piece>(piece));
            while (bb) {
                Square sq = pop_lsb(bb);
                accumulate_piece(state, c, static_cast<Piece>(piece), sq, +1);
                if (piece == static_cast<int>(Piece::Pawn)) {
                    state.pawn_key ^= PawnZobrist[color][static_cast<int>(sq)];
                }
            }
        }
    }
    state.phase = std::clamp(state.phase, 0, PhaseTotal);
    state.pawn_dirty = true;
    state.mobility_dirty = true;
    update_pawn_eval(pos, state);
    update_mobility(pos, state);
    return state;
}

void eval_on_piece_add(EvalState& state, Color color, Piece piece, Square sq) {
    accumulate_piece(state, color, piece, sq, +1);
    if (piece == Piece::Pawn) {
        state.pawn_key ^= PawnZobrist[static_cast<int>(color)][static_cast<int>(sq)];
        state.pawn_dirty = true;
    }
    if (piece == Piece::King) {
        state.pawn_dirty = true;
    }
    state.mobility_dirty = true;
    state.phase = std::clamp(state.phase, 0, PhaseTotal);
}

void eval_on_piece_remove(EvalState& state, Color color, Piece piece, Square sq) {
    accumulate_piece(state, color, piece, sq, -1);
    if (piece == Piece::Pawn) {
        state.pawn_key ^= PawnZobrist[static_cast<int>(color)][static_cast<int>(sq)];
        state.pawn_dirty = true;
    }
    if (piece == Piece::King) {
        state.pawn_dirty = true;
    }
    state.mobility_dirty = true;
    state.phase = std::clamp(state.phase, 0, PhaseTotal);
}

int evaluate(const Position& pos, EvalState& state) {
    if (state.pawn_dirty) {
        update_pawn_eval(pos, state);
    }
    if (state.mobility_dirty) {
        update_mobility(pos, state);
    }

    int mg_score = (state.material_mg[0] - state.material_mg[1]) +
                   (state.psq_mg[0] - state.psq_mg[1]) +
                   (state.pawn_mg[0] - state.pawn_mg[1]) +
                   (state.mobility_mg[0] - state.mobility_mg[1]);

    int eg_score = (state.material_eg[0] - state.material_eg[1]) +
                   (state.psq_eg[0] - state.psq_eg[1]) +
                   (state.pawn_eg[0] - state.pawn_eg[1]) +
                   (state.mobility_eg[0] - state.mobility_eg[1]);

    int phase = std::clamp(state.phase, 0, PhaseTotal);
    int blended = (mg_score * phase + eg_score * (PhaseTotal - phase)) / PhaseTotal;

    constexpr int Tempo = 10;
    blended += Tempo * (pos.side_to_move() == Color::White ? 1 : -1);
    return pos.side_to_move() == Color::White ? blended : -blended;
}

}  // namespace chess
