#include "chess/search.hpp"

#include "chess/evaluation.hpp"

#include <algorithm>
#include <limits>

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

}  // namespace

void TimeManager::start(const SearchLimits& limits, Color side_to_move) {
    limits_ = limits;
    side_to_move_ = side_to_move;
    start_time_ = std::chrono::steady_clock::now();

    soft_limit_ms_ = 0;
    hard_limit_ms_ = 0;
    if (limits_.infinite) {
        return;
    }

    std::uint64_t move_time = limits_.time_ms;
    const int idx = static_cast<int>(side_to_move_);

    if (move_time == 0) {
        const std::uint64_t time_left = limits_.time_left[idx];
        const std::uint64_t inc = limits_.increment[idx];
        if (time_left > 0) {
            int moves_to_go = limits_.moves_to_go > 0 ? limits_.moves_to_go : 30;
            move_time = time_left / std::max(1, moves_to_go + 2);
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
    hard_limit_ms_ = move_time + move_time / 2 + 20;
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

Search::Search(TranspositionTable& tt) : tt_(tt) {
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
    pv_table_.fill(make_null_move());
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
        pv_table_.fill(make_null_move());
        if (stop_) {
            break;
        }

        std::vector<Move> moves = pos.generate_legal_moves();
        if (moves.empty()) {
            result.best_move = make_null_move();
            result.score = pos.in_check(pos.side_to_move()) ? -CheckmateScore + depth : DrawScore;
            result.depth_reached = depth;
            break;
        }

        Move tt_move = make_null_move();
        TTEntry entry{};
        if (tt_.probe(pos.zobrist(), entry)) {
            ++stats_.tt_hits;
            tt_move = entry.best_move;
        }
        if (tt_move.is_null() && depth > 1 && !pv_table_[0].is_null()) {
            tt_move = pv_table_[0];
        }

        std::vector<std::pair<int, Move>> ordered;
        ordered.reserve(moves.size());
        for (const Move& move : moves) {
            ordered.emplace_back(score_move(pos, move, tt_move, 0, pos.side_to_move(), make_null_move()), move);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

        int alpha = -InfiniteScore;
        int beta = InfiniteScore;
        Move best_move = make_null_move();
        int best_score = -InfiniteScore;

        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (stop_ || time_manager_.should_stop()) {
                stop_ = true;
                break;
            }

            const Move& move = ordered[i].second;
            if (!pos.make_move(move)) {
                continue;
            }
            int score = -negamax(pos, depth - 1, -beta, -alpha, 1, true, move);
            pos.unmake_move();

            if (stop_) {
                break;
            }

            if (score > best_score) {
                best_score = score;
                best_move = move;
            }
            if (score > alpha) {
                alpha = score;
            }
            if (alpha >= beta) {
                break;
            }
        }

        if (stop_) {
            break;
        }

        if (!best_move.is_null()) {
            result.best_move = best_move;
            result.score = best_score;
            result.depth_reached = depth;
            pv_table_[0] = best_move;
        }

        if (limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
            stats_.nodes >= limits_.nodes) {
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
    (void)pos;
    int victim = PieceOrder[static_cast<int>(move.capture)];
    int attacker = PieceOrder[static_cast<int>(move.piece)];
    if (move.flags & MoveFlagPromotion) {
        attacker = PieceOrder[static_cast<int>(move.promotion)];
    }
    return victim - attacker;
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

void Search::update_history(Color side, const Move& move, int depth, int delta) {
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

int Search::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allow_null, Move prev_move) {
    if (stop_) {
        return alpha;
    }
    if (limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
        stats_.nodes >= limits_.nodes) {
        stop_ = true;
        return alpha;
    }
    if (time_manager_.should_stop()) {
        stop_ = true;
        return alpha;
    }

    const Color us = pos.side_to_move();
    const bool in_check = pos.in_check(us);

    if (depth <= 0) {
        return quiescence(pos, alpha, beta, ply);
    }

    if (in_check) {
        depth += 1;
    }

    increment_nodes();

    Key key = pos.zobrist();
    TTEntry entry{};
    Move tt_move = make_null_move();
    if (tt_.probe(key, entry)) {
        ++stats_.tt_hits;
        int tt_score = from_tt_score(entry.score, ply);
        if (entry.depth >= depth && !time_manager_.should_stop()) {
            if (entry.bound == Bound::Exact) {
                return tt_score;
            }
            if (entry.bound == Bound::Lower && tt_score > alpha) {
                alpha = tt_score;
            } else if (entry.bound == Bound::Upper && tt_score < beta) {
                beta = tt_score;
            }
            if (alpha >= beta) {
                return tt_score;
            }
        }
        if (!entry.best_move.is_null()) {
            tt_move = entry.best_move;
        }
    }
    if (tt_move.is_null() && !pv_table_[ply].is_null()) {
        tt_move = pv_table_[ply];
    }

    Bitboard non_pawn_material = pos.pieces(us, Piece::Knight) |
                                 pos.pieces(us, Piece::Bishop) |
                                 pos.pieces(us, Piece::Rook) |
                                 pos.pieces(us, Piece::Queen);
    if (allow_null && depth >= 3 && !in_check && non_pawn_material) {
        if (pos.make_null_move()) {
            int score = -negamax(pos, depth - 1 - 2, -beta, -beta + 1, ply + 1, false, make_null_move());
            pos.unmake_null_move();
            if (stop_) {
                return alpha;
            }
            if (score >= beta) {
                tt_.store(key, depth, to_tt_score(beta, ply), Bound::Lower, make_null_move());
                return beta;
            }
        }
    }

    std::vector<Move> moves = pos.generate_legal_moves();
    if (moves.empty()) {
        if (in_check) {
            return -CheckmateScore + ply;
        }
        return DrawScore;
    }

    std::vector<std::pair<int, Move>> ordered;
    ordered.reserve(moves.size());
    for (const Move& move : moves) {
        ordered.emplace_back(score_move(pos, move, tt_move, ply, us, prev_move), move);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

    int original_alpha = alpha;
    Move best_move = make_null_move();
    bool found_pv = false;
    int move_index = 0;

    for (const auto& entry_move : ordered) {
        if (stop_ || time_manager_.should_stop()) {
            stop_ = true;
            break;
        }

        Move move = entry_move.second;
        if (!(move.flags & MoveFlagPromotion)) {
            if (see(pos, move) < 0) {
                continue;
            }
        }
        if (!pos.make_move(move)) {
            continue;
        }

        bool is_capture = (move.flags & MoveFlagCapture) != 0;
        bool is_promo = (move.flags & MoveFlagPromotion) != 0;
        bool is_quiet = !(move.flags & QuietMask);

        int new_depth = depth - 1;
        int reduction = 0;
        if (!found_pv && depth >= 3 && is_quiet && !in_check) {
            reduction = 1 + (move_index >= 6);
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

        if (stop_) {
            break;
        }

        ++move_index;

        if (is_quiet) {
            int history_bonus = depth * depth;
            if (history_bonus <= 0) history_bonus = 1;
            int penalty = std::max(1, history_bonus / 2);
            if (score > alpha) {
                update_history(us, move, depth, history_bonus);
            } else {
                update_history(us, move, depth, -penalty);
            }
        }

        if (score > alpha) {
            alpha = score;
            best_move = move;
            found_pv = true;
            pv_table_[ply] = move;
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

    if (stop_) {
        return alpha;
    }

    if (best_move.is_null() && !ordered.empty()) {
        best_move = ordered.front().second;
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
    if (stop_) {
        return alpha;
    }
    if (limits_.nodes != std::numeric_limits<std::uint64_t>::max() &&
        stats_.nodes >= limits_.nodes) {
        stop_ = true;
        return alpha;
    }
    if (time_manager_.should_stop()) {
        stop_ = true;
        return alpha;
    }

    increment_qnodes();

    int stand_pat = evaluate(pos, pos.eval_state());
    if (stand_pat >= beta) {
        return beta;
    }
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    std::vector<Move> moves = pos.generate_legal_moves();
    for (const Move& move : moves) {
        if (!(move.flags & MoveFlagCapture) && !(move.flags & MoveFlagPromotion)) {
            continue;
        }
        if (!pos.make_move(move)) {
            continue;
        }
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.unmake_move();

        if (stop_) {
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

}  // namespace chess
