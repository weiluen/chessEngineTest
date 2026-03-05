/**
 * Search framework (iterative deepening negamax with transposition support).
 */
#pragma once

#include "position.hpp"
#include "transposition_table.hpp"
#include "types.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace chess {

struct SearchStatistics {
    std::uint64_t nodes = 0;
    std::uint64_t qnodes = 0;
    std::uint64_t tb_hits = 0;
    std::uint64_t tt_hits = 0;
};

struct SearchResult {
    Move best_move = make_null_move();
    int score = DrawScore;
    SearchStatistics stats{};
    int depth_reached = 0;
    std::vector<Move> pv;
};

class TimeManager {
public:
    void start(const SearchLimits& limits, Color side_to_move);
    [[nodiscard]] bool should_stop() const;
    [[nodiscard]] bool soft_stop() const;
    void update_score(int score);

private:
    SearchLimits limits_{};
    std::chrono::steady_clock::time_point start_time_{};
    std::uint64_t soft_limit_ms_ = 0;
    std::uint64_t hard_limit_ms_ = 0;
    Color side_to_move_ = Color::White;
    int prev_score_ = 0;
    bool prev_score_valid_ = false;
};

class Search {
public:
    explicit Search(TranspositionTable& tt);

    SearchResult iterative_deepening(Position& pos, const SearchLimits& limits);

    void stop();
    [[nodiscard]] std::uint64_t node_count() const noexcept;

private:
    int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allow_null, Move prev_move, Move excluded_move = {});
    int quiescence(Position& pos, int alpha, int beta, int ply);
    int see(const Position& pos, Move move) const;
    int score_move(const Position& pos, const Move& move, const Move& tt_move, int ply, Color side, Move prev_move) const;
    void update_history(Color side, const Move& move, int delta);
    void update_killers(int ply, const Move& move);
    void update_capture_history(Color side, Piece piece, Square to, int delta);
    void decay_history();

    void increment_nodes() noexcept { ++stats_.nodes; }
    void increment_qnodes() noexcept { ++stats_.qnodes; }

    static int lmr_table_[64][64];
    static bool lmr_initialized_;
    static void init_lmr();

    TranspositionTable& tt_;
    SearchStatistics stats_{};
    TimeManager time_manager_{};
    std::atomic<bool> stop_{false};
    SearchLimits limits_{};
    std::array<std::array<std::array<int, 64>, 64>, 2> history_{};
    std::array<std::array<std::array<int, 64>, 6>, 2> capture_history_{};
    std::array<std::array<Move, 64>, 2> counter_moves_{};
    // Countermove history: [prev_piece][prev_to][piece][to]
    std::array<std::array<std::array<std::array<int, 64>, 6>, 64>, 6> cmh_{};
    std::array<std::array<Move, MaxPly>, MaxPly> pv_table_{};
    std::array<int, MaxPly> pv_length_{};
    std::array<std::array<Move, 2>, MaxPly> killers_{};
};

class SearchPool {
public:
    explicit SearchPool(TranspositionTable& tt, int num_threads = 1);

    void set_threads(int num_threads);
    SearchResult search(Position& pos, const SearchLimits& limits);
    void stop();

private:
    TranspositionTable& tt_;
    std::vector<std::unique_ptr<Search>> workers_;
    int num_threads_;
};

}  // namespace chess
