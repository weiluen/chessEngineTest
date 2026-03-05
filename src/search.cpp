#include "chess/search.hpp"

#include "chess/evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

namespace chess {

namespace {

inline int to_tt_score(int score, int ply) {
    if (score > CheckmateThreshold) {
        return score + ply;
    }
    if (score < -CheckmateThreshold) {
        return score - ply;
    }
    return score;
}

inline int from_tt_score(int score, int ply) {
    if (score > CheckmateThreshold) {
        return score - ply;
    }
    if (score < -CheckmateThreshold) {
        return score + ply;
    }
    return score;
}

inline bool same_move(const Move& a, const Move& b) {
    return a.from == b.from &&
           a.to == b.to &&
           a.piece == b.piece &&
           a.promotion == b.promotion &&
           a.flags == b.flags;
}

constexpr int PieceOrder[7] = {100, 320, 330, 500, 900, 20000, 0};

constexpr std::uint8_t QuietMask = MoveFlagCapture | MoveFlagPromotion | MoveFlagEnPassant;
constexpr int FutilityMargins[4] = {0, 120, 200, 320};
constexpr int RazorMargin = 200;
constexpr int SEE_CAPTURE_THRESHOLD = 0;
constexpr int DeltaMargin = 90;
constexpr int RFP_MARGIN = 100;  // Reverse Futility Pruning margin per depth (relaxed to reduce over-pruning)

// Late Move Pruning thresholds by depth
constexpr int LmpThresholds[7] = {0, 7, 12, 18, 26, 36, 50};

// Per-ply static eval storage for "improving" heuristic
thread_local int static_eval_stack[MaxPly + 4] = {};

// Scored move for incremental picking
struct ScoredMove {
    int score;
    int index;  // index into MoveList
};

}  // namespace

void TimeManager::start(const SearchLimits& limits, Color side_to_move) {
    limits_ = limits;
    side_to_move_ = side_to_move;
    start_time_ = std::chrono::steady_clock::now();

    soft_limit_ms_ = 0;
    hard_limit_ms_ = 0;
    prev_score_ = 0;
    prev_score_valid_ = false;
    if (limits_.infinite) {
        return;
    }

    std::uint64_t move_time = limits_.time_ms;
    const int idx = static_cast<int>(side_to_move_);

    if (move_time == 0) {
        const std::uint64_t time_left = limits_.time_left[idx];
        const std::uint64_t inc = limits_.increment[idx];
        if (time_left > 0) {
            int moves_to_go = limits_.moves_to_go > 0 ? limits_.moves_to_go : 25;
            move_time = time_left / std::max(1, moves_to_go);
            move_time += inc / 2;
            std::uint64_t safety = std::max<std::uint64_t>(50, time_left / 50);
            if (move_time + safety >= time_left) {
                move_time = (time_left > safety) ? time_left - safety : time_left / 2;
            }
        } else if (inc > 0) {
            move_time = std::max<std::uint64_t>(inc - 5, inc * 3 / 4);
        }
    }

    if (move_time == 0) {
        return;
    }

    soft_limit_ms_ = move_time;
    hard_limit_ms_ = move_time * 5 / 2 + 20;
}

bool TimeManager::should_stop() const {
    if (hard_limit_ms_ == 0) {
        return false;
    }
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return elapsed_ms >= static_cast<long long>(hard_limit_ms_);
}

bool TimeManager::soft_stop() const {
    if (soft_limit_ms_ == 0) {
        return false;
    }
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return elapsed_ms >= static_cast<long long>(soft_limit_ms_);
}

void TimeManager::update_score(int score) {
    if (prev_score_valid_ && score < prev_score_ - 50) {
        // Extend soft limit when score drops significantly (panic mode)
        soft_limit_ms_ = soft_limit_ms_ * 3 / 2;
    }
    prev_score_ = score;
    prev_score_valid_ = true;
}

int Search::lmr_table_[64][64]{};
bool Search::lmr_initialized_ = false;

void Search::init_lmr() {
    if (lmr_initialized_) return;
    for (int d = 1; d < 64; ++d) {
        for (int m = 1; m < 64; ++m) {
            lmr_table_[d][m] = static_cast<int>(0.75 + std::log(d) * std::log(m) * 0.4);
        }
    }
    lmr_initialized_ = true;
}

Search::Search(TranspositionTable& tt) : tt_(tt) {
    init_lmr();
    for (auto& ply : killers_) {
        ply[0] = ply[1] = make_null_move();
    }
    for (auto& color : history_) {
        for (auto& from : color) {
            from.fill(0);
        }
    }
    for (auto& color : counter_moves_) {
        for (auto& move : color) {
            move = make_null_move();
        }
    }
    for (auto& color : capture_history_) {
        for (auto& piece_table : color) {
            for (auto& entry : piece_table) {
                entry = 0;
            }
        }
    }
    pv_length_.fill(0);
}

void Search::decay_history() {
    for (auto& color : history_) {
        for (auto& from : color) {
            for (int& entry : from) {
                entry /= 2;
            }
        }
    }
    for (auto& ply : killers_) {
        ply[0] = ply[1] = make_null_move();
    }
    for (auto& color : counter_moves_) {
        for (auto& move : color) {
            move = make_null_move();
        }
    }
    for (auto& color : capture_history_) {
        for (auto& piece_table : color) {
            for (int& entry : piece_table) {
                entry /= 2;
            }
        }
    }
}

SearchResult Search::iterative_deepening(Position& pos, const SearchLimits& limits) {
    limits_ = limits;
    stats_ = {};
    stop_.store(false, std::memory_order_relaxed);
    decay_history();
    time_manager_.start(limits, pos.side_to_move());
    tt_.new_search();

    SearchResult result{};
    result.best_move = make_null_move();
    result.score = DrawScore;
    result.depth_reached = 0;

    int max_depth = limits.depth > 0 ? limits.depth : 64;
    if (max_depth > MaxPly - 2) {
        max_depth = MaxPly - 2;
    }

    for (int depth = 1; depth <= max_depth; ++depth) {
        pv_length_.fill(0);
        if (UNLIKELY(stop_)) {
            break;
        }

        MoveList moves = pos.generate_legal_moves();
        if (UNLIKELY(moves.empty())) {
            result.best_move = make_null_move();
            result.score = pos.in_check(pos.side_to_move()) ? -CheckmateScore + depth : DrawScore;
            result.depth_reached = depth;
            break;
        }

        Move tt_move = make_null_move();
        tt_.prefetch(pos.zobrist());
        TTEntry entry{};
        if (tt_.probe(pos.zobrist(), entry)) {
            ++stats_.tt_hits;
            tt_move = entry.best_move();
        }
        // Use previous iteration's PV move as fallback
        if (tt_move.is_null() && !result.pv.empty()) {
            tt_move = result.pv[0];
        }

        // Score and sort root moves
        std::array<int, MaxMoves> scores{};
        for (int i = 0; i < moves.size(); ++i) {
            scores[static_cast<std::size_t>(i)] = score_move(pos, moves[i], tt_move, 0, pos.side_to_move(), make_null_move());
        }
        // Full sort at root (small number of moves, worth it)
        for (int i = 0; i < moves.size() - 1; ++i) {
            int best_idx = i;
            for (int j = i + 1; j < moves.size(); ++j) {
                if (scores[static_cast<std::size_t>(j)] > scores[static_cast<std::size_t>(best_idx)]) {
                    best_idx = j;
                }
            }
            if (best_idx != i) {
                std::swap(moves[i], moves[best_idx]);
                std::swap(scores[static_cast<std::size_t>(i)], scores[static_cast<std::size_t>(best_idx)]);
            }
        }

        // Aspiration windows for depth >= 5 - exponential widening
        int alpha, beta;
        int asp_delta = 16;
        if (depth >= 5 && result.depth_reached > 0) {
            alpha = result.score - asp_delta;
            beta = result.score + asp_delta;
        } else {
            alpha = -InfiniteScore;
            beta = InfiniteScore;
        }

        while (true) {
            Move best_move = make_null_move();
            int best_score = -InfiniteScore;
            int local_alpha = alpha;
            pv_length_[0] = 0;

            for (int i = 0; i < moves.size(); ++i) {
                if (UNLIKELY(stop_ || time_manager_.should_stop())) {
                    stop_ = true;
                    break;
                }

                const Move& move = moves[i];
                if (!pos.make_move(move)) {
                    continue;
                }
                tt_.prefetch(pos.zobrist());
                int score = -negamax(pos, depth - 1, -beta, -local_alpha, 1, true, move);
                pos.unmake_move();

                if (UNLIKELY(stop_)) {
                    break;
                }

                if (score > best_score) {
                    best_score = score;
                    best_move = move;
                }
                if (score > local_alpha) {
                    local_alpha = score;
                    // Update triangular PV at root
                    pv_table_[0][0] = move;
                    int child_len = std::min(pv_length_[1], MaxPly - 2);
                    for (int j = 0; j < child_len; ++j) {
                        pv_table_[0][j + 1] = pv_table_[1][j];
                    }
                    pv_length_[0] = child_len + 1;
                }
                if (local_alpha >= beta) {
                    break;
                }
            }

            if (UNLIKELY(stop_)) {
                break;
            }

            // Check for aspiration window failure - exponential widening
            if (depth >= 5 && result.depth_reached > 0) {
                if (best_score <= alpha) {
                    asp_delta *= 4;
                    if (asp_delta > 512) {
                        alpha = -InfiniteScore;
                    } else {
                        alpha = result.score - asp_delta;
                    }
                    // Re-order: move the best move from failed search to front
                    if (!best_move.is_null()) {
                        for (int i = 1; i < moves.size(); ++i) {
                            if (same_move(moves[i], best_move)) {
                                for (int j = i; j > 0; --j) {
                                    std::swap(moves[j], moves[j - 1]);
                                    std::swap(scores[static_cast<std::size_t>(j)], scores[static_cast<std::size_t>(j - 1)]);
                                }
                                break;
                            }
                        }
                    }
                    continue;
                }
                if (best_score >= beta) {
                    asp_delta *= 4;
                    if (asp_delta > 512) {
                        beta = InfiniteScore;
                    } else {
                        beta = result.score + asp_delta;
                    }
                    // Re-order: move the best move from failed search to front
                    if (!best_move.is_null()) {
                        for (int i = 1; i < moves.size(); ++i) {
                            if (same_move(moves[i], best_move)) {
                                for (int j = i; j > 0; --j) {
                                    std::swap(moves[j], moves[j - 1]);
                                    std::swap(scores[static_cast<std::size_t>(j)], scores[static_cast<std::size_t>(j - 1)]);
                                }
                                break;
                            }
                        }
                    }
                    continue;
                }
            }

            if (!best_move.is_null()) {
                result.best_move = best_move;
                result.score = best_score;
                result.depth_reached = depth;
                result.pv.clear();
                for (int j = 0; j < pv_length_[0]; ++j) {
                    result.pv.push_back(pv_table_[0][j]);
                }
            }
            break;
        }

        if (UNLIKELY(stop_)) {
            break;
        }

        // Update time manager with score for panic mode detection
        time_manager_.update_score(result.score);

        if (UNLIKELY(limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
            stats_.nodes >= limits_.nodes)) {
            stop_ = true;
        }
        if (time_manager_.soft_stop()) {
            stop_ = true;
        }
    }

    result.stats = stats_;
    return result;
}

int Search::see(const Position& pos, Move move) const {
    Square to = static_cast<Square>(move.to);
    Square from = static_cast<Square>(move.from);

    constexpr int SeeVal[7] = {100, 320, 330, 500, 900, 20000, 0};

    int gain[32];
    int depth_see = 0;

    Bitboard occ = pos.occupancy();
    Bitboard from_bb = bit(from);

    Piece attacker_piece = move.piece;
    if (move.flags & MoveFlagEnPassant) {
        gain[0] = SeeVal[static_cast<int>(Piece::Pawn)];
        Color them = opposite(pos.side_to_move());
        int ep_capture_sq = static_cast<int>(to) + (pos.side_to_move() == Color::White ? -8 : 8);
        occ ^= bit(static_cast<Square>(ep_capture_sq));
    } else if (move.capture != Piece::None) {
        gain[0] = SeeVal[static_cast<int>(move.capture)];
    } else if (move.flags & MoveFlagPromotion) {
        gain[0] = SeeVal[static_cast<int>(move.promotion)] - SeeVal[static_cast<int>(Piece::Pawn)];
    } else {
        gain[0] = 0;
    }

    occ ^= from_bb;

    int attacker_val = SeeVal[static_cast<int>(attacker_piece)];
    Color side = opposite(pos.side_to_move());

    Bitboard diag_sliders = (pos.pieces(Color::White, Piece::Bishop) | pos.pieces(Color::Black, Piece::Bishop) |
                              pos.pieces(Color::White, Piece::Queen)  | pos.pieces(Color::Black, Piece::Queen));
    Bitboard orth_sliders = (pos.pieces(Color::White, Piece::Rook)   | pos.pieces(Color::Black, Piece::Rook) |
                              pos.pieces(Color::White, Piece::Queen)  | pos.pieces(Color::Black, Piece::Queen));

    auto get_all_attackers = [&](Square sq, Bitboard occupancy) -> Bitboard {
        return (pawn_attacks(Color::White, sq) & pos.pieces(Color::Black, Piece::Pawn)) |
               (pawn_attacks(Color::Black, sq) & pos.pieces(Color::White, Piece::Pawn)) |
               (knight_attacks(sq) & (pos.pieces(Color::White, Piece::Knight) | pos.pieces(Color::Black, Piece::Knight))) |
               (bishop_attacks(sq, occupancy) & diag_sliders) |
               (rook_attacks(sq, occupancy) & orth_sliders) |
               (king_attacks(sq) & (pos.pieces(Color::White, Piece::King) | pos.pieces(Color::Black, Piece::King)));
    };

    Bitboard attackers = get_all_attackers(to, occ) & occ;

    while (true) {
        ++depth_see;
        gain[depth_see] = attacker_val - gain[depth_see - 1];

        if (std::max(-gain[depth_see - 1], gain[depth_see]) < 0) {
            break;
        }

        Bitboard side_attackers = attackers & pos.occupancy(side);
        if (!side_attackers) break;

        Piece lva = Piece::None;
        Bitboard lva_bb = 0;
        for (int p = 0; p < 6; ++p) {
            Bitboard candidates = side_attackers & pos.pieces(side, static_cast<Piece>(p));
            if (candidates) {
                lva = static_cast<Piece>(p);
                lva_bb = candidates & (~candidates + 1);
                break;
            }
        }
        if (lva == Piece::None) break;

        attacker_val = SeeVal[static_cast<int>(lva)];
        occ ^= lva_bb;

        if (lva == Piece::Pawn || lva == Piece::Bishop || lva == Piece::Queen) {
            attackers |= bishop_attacks(to, occ) & diag_sliders;
        }
        if (lva == Piece::Rook || lva == Piece::Queen) {
            attackers |= rook_attacks(to, occ) & orth_sliders;
        }
        attackers &= occ;

        side = opposite(side);

        if (depth_see >= 31) break;
    }

    while (--depth_see > 0) {
        gain[depth_see - 1] = -std::max(-gain[depth_see - 1], gain[depth_see]);
    }

    return gain[0];
}

int Search::score_move(const Position& pos, const Move& move, const Move& tt_move, int ply, Color side, Move prev_move) const {
    if (!tt_move.is_null() && same_move(move, tt_move)) {
        return 1'000'000;
    }

    bool is_capture = (move.flags & MoveFlagCapture) != 0;
    bool is_promo = (move.flags & MoveFlagPromotion) != 0;
    bool is_en_passant = (move.flags & MoveFlagEnPassant) != 0;

    if (is_capture || is_en_passant) {
        int score = 500'000;
        int see_score = see(pos, move);
        score += std::clamp(see_score, -5000, 5000);
        int capture_hist = capture_history_[static_cast<int>(side)][static_cast<int>(move.piece)][move.to];
        score += capture_hist;
        if (is_promo) {
            score += 4'000;
        }
        if (move.flags & MoveFlagCheck) {
            score += 500;
        }
        return score;
    }

    if (is_promo) {
        return 300'000 + PieceOrder[static_cast<int>(move.promotion)];
    }

    if (ply < MaxPly) {
        if (same_move(move, killers_[ply][0])) {
            return 250'000;
        }
        if (same_move(move, killers_[ply][1])) {
            return 240'000;
        }
    }

    if (!prev_move.is_null()) {
        Move counter = counter_moves_[static_cast<int>(side)][prev_move.to];
        if (!counter.is_null() && same_move(move, counter)) {
            return 230'000;
        }
    }

    int history = history_[static_cast<int>(side)][move.from][move.to];
    if (move.flags & MoveFlagCheck) {
        history += 50;
    }
    return history;
}

void Search::update_history(Color side, const Move& move, int delta) {
    if (move.flags & QuietMask) {
        return;
    }
    int& entry = history_[static_cast<int>(side)][move.from][move.to];
    entry = std::clamp(entry + delta, -40000, 40000);
}

void Search::update_killers(int ply, const Move& move) {
    if (move.flags & QuietMask) {
        return;
    }
    if (ply >= MaxPly) {
        return;
    }
    if (!same_move(move, killers_[ply][0])) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = move;
    }
}

void Search::update_capture_history(Color side, Piece piece, Square to, int delta) {
    int attacker = static_cast<int>(piece);
    int target = static_cast<int>(to);
    int& entry = capture_history_[static_cast<int>(side)][attacker][target];
    entry = std::clamp(entry + delta, -40000, 40000);
}

int Search::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allow_null, Move prev_move, Move excluded_move) {
    if (UNLIKELY(stop_)) {
        return alpha;
    }
    if (UNLIKELY(limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
        stats_.nodes >= limits_.nodes)) {
        stop_ = true;
        return alpha;
    }
    if (UNLIKELY(time_manager_.should_stop())) {
        stop_ = true;
        return alpha;
    }

    // Ply overflow protection
    if (UNLIKELY(ply >= MaxPly - 1)) {
        return evaluate(pos, pos.eval_state());
    }

    // Init PV length for this ply
    pv_length_[ply] = 0;

    const Color us = pos.side_to_move();
    const bool in_check = pos.in_check(us);

    if (UNLIKELY(depth <= 0)) {
        return quiescence(pos, alpha, beta, ply);
    }

    if (in_check) {
        depth += 1;
    }

    // Repetition detection (twofold) and fifty-move rule
    if (ply > 0) {
        if (pos.is_repetition(ply)) {
            return DrawScore;
        }
        if (pos.fifty_move_counter() >= 100) {
            return DrawScore;
        }
    }

    increment_nodes();

    Key key = pos.zobrist();
    tt_.prefetch(key);
    TTEntry entry{};
    Move tt_move = make_null_move();
    int tt_score = 0;
    int tt_depth = -1;
    Bound tt_bound = Bound::None;
    bool tt_hit = false;
    if (tt_.probe(key, entry)) {
        ++stats_.tt_hits;
        tt_score = from_tt_score(entry.score, ply);
        tt_depth = entry.depth;
        tt_bound = entry.bound();
        tt_hit = true;
        if (entry.depth >= depth && !time_manager_.should_stop() && excluded_move.is_null()) {
            if (entry.bound() == Bound::Exact) {
                return tt_score;
            }
            if (entry.bound() == Bound::Lower && tt_score > alpha) {
                alpha = tt_score;
            } else if (entry.bound() == Bound::Upper && tt_score < beta) {
                beta = tt_score;
            }
            if (alpha >= beta) {
                return tt_score;
            }
        }
        Move entry_move = entry.best_move();
        if (!entry_move.is_null()) {
            tt_move = entry_move;
        }
    }

    // IID: if no TT move at sufficient depth, do a reduced search
    if (depth >= 5 && tt_move.is_null()) {
        int iid_depth = depth - 2;
        if (iid_depth > 0) {
            int iid_score = negamax(pos, iid_depth, alpha, beta, ply, false, prev_move);
            if (UNLIKELY(stop_)) {
                return alpha;
            }
            // Re-probe TT after IID
            TTEntry iid_entry{};
            if (tt_.probe(key, iid_entry)) {
                Move iid_move = iid_entry.best_move();
                if (!iid_move.is_null()) {
                    tt_move = iid_move;
                }
            }
            (void)iid_score;
        }
    }

    int static_eval = evaluate(pos, pos.eval_state());

    // Store static eval for "improving" heuristic
    if (LIKELY(ply < MaxPly)) {
        static_eval_stack[ply] = static_eval;
    }

    // Determine if the side's eval is improving (compared to 2 plies ago)
    bool improving = (ply >= 2) ? (static_eval > static_eval_stack[ply - 2]) : true;

    bool is_pv_node = (beta - alpha > 1);

    // Reverse Futility Pruning (Static Null Move Pruning)
    if (!in_check && !is_pv_node && depth <= 7 && excluded_move.is_null() &&
        static_eval - RFP_MARGIN * depth >= beta &&
        std::abs(beta) < CheckmateThreshold) {
        return static_eval;
    }

    if (!in_check && depth <= 1 && static_eval + RazorMargin <= alpha) {
        int q = quiescence(pos, alpha, beta, ply);
        if (q <= alpha) {
            return q;
        }
    }

    Bitboard non_pawn_material = pos.pieces(us, Piece::Knight) |
                                 pos.pieces(us, Piece::Bishop) |
                                 pos.pieces(us, Piece::Rook) |
                                 pos.pieces(us, Piece::Queen);

    // Null-move pruning with adaptive R
    if (allow_null && depth >= 3 && !in_check && non_pawn_material && excluded_move.is_null()) {
        int R = 3 + depth / 3 + std::min(3, (static_eval - beta) / 200);
        R = std::min(R, depth - 1);
        R = std::max(R, 1);
        if (pos.make_null_move()) {
            tt_.prefetch(pos.zobrist());
            int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false, make_null_move());
            pos.unmake_null_move();
            if (UNLIKELY(stop_)) {
                return alpha;
            }
            if (score >= beta) {
                if (depth >= 8) {
                    int verification = -negamax(pos, depth - 1 - 1, -beta, -beta + 1, ply + 1, false, make_null_move());
                    if (verification >= beta) {
                        tt_.store(key, depth, to_tt_score(beta, ply), Bound::Lower, make_null_move());
                        return beta;
                    }
                } else {
                    tt_.store(key, depth, to_tt_score(beta, ply), Bound::Lower, make_null_move());
                    return beta;
                }
            }
        }
    }

    // Singular extension
    bool tt_move_is_singular = false;
    if (depth >= 8 && !tt_move.is_null() && tt_hit &&
        excluded_move.is_null() &&
        tt_depth >= depth - 3 &&
        (tt_bound == Bound::Lower || tt_bound == Bound::Exact) &&
        std::abs(tt_score) < CheckmateThreshold) {
        int singular_beta = tt_score - depth * 3;
        int se_depth = (depth - 1) / 2;

        int se_score = negamax(pos, se_depth, singular_beta - 1, singular_beta, ply, false, prev_move, tt_move);
        if (se_score < singular_beta) {
            tt_move_is_singular = true;
        }
    }

    MoveList moves = pos.generate_legal_moves();
    if (UNLIKELY(moves.empty())) {
        if (in_check) {
            return -CheckmateScore + ply;
        }
        return DrawScore;
    }

    // Score all moves
    std::array<int, MaxMoves> move_scores{};
    for (int i = 0; i < moves.size(); ++i) {
        move_scores[static_cast<std::size_t>(i)] = score_move(pos, moves[i], tt_move, ply, us, prev_move);
    }

    int original_alpha = alpha;
    Move best_move = make_null_move();
    bool found_pv = false;
    int move_index = 0;
    int num_moves = moves.size();

    for (int pick = 0; pick < num_moves; ++pick) {
        if (UNLIKELY(stop_ || time_manager_.should_stop())) {
            stop_ = true;
            break;
        }

        // Incremental move picking: find best remaining and swap to current position
        {
            int best_idx = pick;
            for (int j = pick + 1; j < num_moves; ++j) {
                if (move_scores[static_cast<std::size_t>(j)] > move_scores[static_cast<std::size_t>(best_idx)]) {
                    best_idx = j;
                }
            }
            if (best_idx != pick) {
                std::swap(moves[pick], moves[best_idx]);
                std::swap(move_scores[static_cast<std::size_t>(pick)], move_scores[static_cast<std::size_t>(best_idx)]);
            }
        }

        Move move = moves[pick];
        int move_score = move_scores[static_cast<std::size_t>(pick)];

        // Skip excluded move (used by singular extension verification search)
        if (!excluded_move.is_null() && same_move(move, excluded_move)) {
            continue;
        }

        bool is_capture = (move.flags & MoveFlagCapture) != 0;
        bool is_promo = (move.flags & MoveFlagPromotion) != 0;
        bool gives_check = (move.flags & MoveFlagCheck) != 0;
        bool is_quiet = !(move.flags & QuietMask);
        bool is_tt_move = !tt_move.is_null() && same_move(move, tt_move);

        // Late Move Pruning: skip quiet moves beyond threshold (use improving flag)
        if (!in_check && is_quiet && !gives_check && !is_tt_move && depth <= 6 && depth >= 1 &&
            move_index >= LmpThresholds[depth] * (1 + static_cast<int>(improving))) {
            ++move_index;
            continue;
        }

        // History pruning
        if (!in_check && is_quiet && !gives_check && !is_tt_move && depth <= 4) {
            int hist = history_[static_cast<int>(us)][move.from][move.to];
            if (hist < -4000) {
                ++move_index;
                continue;
            }
        }

        // Futility pruning with improving adjustment
        if (!in_check && is_quiet && depth <= 2 && !gives_check) {
            if (static_eval + FutilityMargins[depth] * (2 - static_cast<int>(improving)) <= alpha) {
                ++move_index;
                continue;
            }
        }

        if (!pos.make_move(move)) {
            ++move_index;
            continue;
        }

        // Prefetch TT for the new position before recursing
        tt_.prefetch(pos.zobrist());

        int new_depth = depth - 1;

        // Singular extension: extend TT move if singular
        if (is_tt_move && tt_move_is_singular) {
            new_depth += 1;
        }

        // Check extension: extend moves that give check
        if (gives_check && depth > 3 && !(is_quiet && move_score < 0)) {
            new_depth += 1;
        }

        int reduction = 0;
        // LMR for bad captures
        if (depth >= 3 && move_index >= 3 && is_capture && !is_tt_move) {
            int see_val = see(pos, move);
            if (see_val < 0) {
                reduction = 1;
                new_depth = std::max(1, depth - 1 - reduction);
            }
        }
        if (depth >= 3 && move_index >= 3 && !in_check && is_quiet) {
            int d = std::min(depth, 63);
            int m = std::min(move_index, 63);
            reduction = lmr_table_[d][m];

            // History-based LMR adjustments
            int hist = history_[static_cast<int>(us)][move.from][move.to];
            if (hist < 0) {
                reduction += 1;  // Reduce MORE for negative history
            }
            if (hist > 4000) {
                reduction -= 1;  // Reduce LESS for good history
            }

            // Reduce LESS for killer moves
            if (LIKELY(ply < MaxPly) &&
                (same_move(move, killers_[ply][0]) || same_move(move, killers_[ply][1]))) {
                reduction -= 1;
            }

            // Reduce MORE if not improving
            if (!improving) {
                reduction += 1;
            }

            reduction = std::max(1, std::min(reduction, depth - 2));
            new_depth = std::max(1, depth - 1 - reduction);
        }

        int score;
        if (found_pv) {
            score = -negamax(pos, new_depth, -alpha - 1, -alpha, ply + 1, true, move);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true, move);
            }
        } else {
            score = -negamax(pos, new_depth, -beta, -alpha, ply + 1, true, move);
        }

        pos.unmake_move();

        if (UNLIKELY(stop_)) {
            break;
        }

        ++move_index;

        if (is_quiet) {
            int history_bonus = depth * depth;
            if (history_bonus <= 0) history_bonus = 1;
            int penalty = std::max(1, history_bonus / 2);
            if (score > alpha) {
                update_history(us, move, history_bonus);
            } else {
                update_history(us, move, -penalty);
            }
        }

        if (is_capture) {
            int capture_bonus = std::max(1, depth * depth);
            if (score > alpha) {
                update_capture_history(us, move.piece, static_cast<Square>(move.to), capture_bonus);
            } else {
                update_capture_history(us, move.piece, static_cast<Square>(move.to), -std::max(1, capture_bonus / 2));
            }
        }

        if (score > alpha) {
            alpha = score;
            best_move = move;
            found_pv = true;

            // Update triangular PV
            if (LIKELY(ply < MaxPly)) {
                pv_table_[ply][0] = move;
                int child_len = (ply + 1 < MaxPly) ? pv_length_[ply + 1] : 0;
                for (int j = 0; j < child_len && j + 1 < MaxPly; ++j) {
                    pv_table_[ply][j + 1] = pv_table_[ply + 1][j];
                }
                pv_length_[ply] = std::min(child_len + 1, MaxPly - 1);
            }

            if (alpha >= beta) {
                if (is_quiet) {
                    update_killers(ply, move);
                    if (!prev_move.is_null()) {
                        counter_moves_[static_cast<int>(us)][prev_move.to] = move;
                    }
                }
                tt_.store(key, depth, to_tt_score(alpha, ply), Bound::Lower, move);
                return alpha;
            }
        }
    }

    if (UNLIKELY(stop_)) {
        return alpha;
    }

    if (best_move.is_null() && num_moves > 0) {
        best_move = moves[0];
    }

    Bound bound = Bound::Exact;
    if (!found_pv) {
        bound = Bound::Upper;
        alpha = original_alpha;
    } else if (alpha <= original_alpha) {
        bound = Bound::Upper;
    }

    tt_.store(key, depth, to_tt_score(alpha, ply), bound, best_move);
    return alpha;
}

int Search::quiescence(Position& pos, int alpha, int beta, int ply) {
    if (UNLIKELY(stop_)) {
        return alpha;
    }
    if (UNLIKELY(limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
        stats_.nodes >= limits_.nodes)) {
        stop_ = true;
        return alpha;
    }
    if (UNLIKELY(time_manager_.should_stop())) {
        stop_ = true;
        return alpha;
    }

    // Ply overflow protection in quiescence
    if (UNLIKELY(ply >= MaxPly - 1)) {
        return evaluate(pos, pos.eval_state());
    }

    increment_qnodes();

    int stand_pat = evaluate(pos, pos.eval_state());
    if (stand_pat >= beta) {
        return beta;
    }
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    MoveList moves = pos.generate_captures();
    for (int i = 0; i < moves.size(); ++i) {
        const Move& move = moves[i];
        int piece_gain = 0;
        if (move.flags & MoveFlagPromotion) {
            piece_gain = PieceOrder[static_cast<int>(move.promotion)];
        } else {
            piece_gain = PieceOrder[static_cast<int>(move.capture)];
        }
        if (!(move.flags & MoveFlagPromotion)) {
            if (stand_pat + piece_gain + DeltaMargin < alpha) {
                continue;
            }
            if (see(pos, move) < SEE_CAPTURE_THRESHOLD) {
                continue;
            }
        }
        if (!pos.make_move(move)) {
            continue;
        }
        tt_.prefetch(pos.zobrist());
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.unmake_move();

        if (UNLIKELY(stop_)) {
            return alpha;
        }

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

void Search::stop() {
    stop_.store(true, std::memory_order_relaxed);
}

std::uint64_t Search::node_count() const noexcept {
    return stats_.nodes + stats_.qnodes;
}

// --- SearchPool (Lazy SMP) ---

SearchPool::SearchPool(TranspositionTable& tt, int num_threads)
    : tt_(tt), num_threads_(std::max(1, num_threads)) {
    workers_.reserve(static_cast<std::size_t>(num_threads_));
    for (int i = 0; i < num_threads_; ++i) {
        workers_.push_back(std::make_unique<Search>(tt_));
    }
}

void SearchPool::set_threads(int num_threads) {
    num_threads_ = std::max(1, num_threads);
    workers_.clear();
    workers_.reserve(static_cast<std::size_t>(num_threads_));
    for (int i = 0; i < num_threads_; ++i) {
        workers_.push_back(std::make_unique<Search>(tt_));
    }
}

SearchResult SearchPool::search(Position& pos, const SearchLimits& limits) {
    if (num_threads_ <= 1) {
        return workers_[0]->iterative_deepening(pos, limits);
    }

    std::vector<std::thread> helper_threads;
    std::vector<Position> helper_positions;
    helper_positions.reserve(static_cast<std::size_t>(num_threads_ - 1));

    SearchLimits helper_limits = limits;
    helper_limits.infinite = true;
    helper_limits.time_ms = 0;
    helper_limits.time_left[0] = 0;
    helper_limits.time_left[1] = 0;
    helper_limits.increment[0] = 0;
    helper_limits.increment[1] = 0;

    for (int i = 1; i < num_threads_; ++i) {
        helper_positions.push_back(pos);
    }

    for (int i = 1; i < num_threads_; ++i) {
        int idx = i;
        helper_threads.emplace_back([this, idx, &helper_positions, &helper_limits]() {
            workers_[idx]->iterative_deepening(helper_positions[idx - 1], helper_limits);
        });
    }

    SearchResult result = workers_[0]->iterative_deepening(pos, limits);

    for (int i = 1; i < num_threads_; ++i) {
        workers_[i]->stop();
    }

    for (auto& t : helper_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    for (int i = 1; i < num_threads_; ++i) {
        result.stats.nodes += workers_[i]->node_count();
    }

    return result;
}

void SearchPool::stop() {
    for (auto& worker : workers_) {
        worker->stop();
    }
}

}  // namespace chess
