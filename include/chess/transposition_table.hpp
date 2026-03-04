/**
 * Lock-free transposition table tuned for cache-friendly probing.
 */
#pragma once

#include "move.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace chess {

enum class Bound : std::uint8_t { Exact, Lower, Upper, None };

struct TTEntry {
    Key key = 0;
    Move best_move = make_null_move();
    std::int16_t score = 0;
    std::int16_t depth = -1;
    Bound bound = Bound::None;
    std::uint8_t generation = 0;
};

struct TTBucket {
    static constexpr std::size_t kSize = 4;
    std::array<TTEntry, kSize> entries{};
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    void resize(std::size_t megabytes);
    void clear();
    void new_search();

    void store(Key key, int depth, int score, Bound bound, Move move);
    [[nodiscard]] bool probe(Key key, TTEntry& entry) const;
    void prefetch(Key key) const;

    struct Stats {
        std::uint64_t lookups = 0;
        std::uint64_t hits = 0;
        std::uint64_t stores = 0;
        std::uint64_t replacements = 0;
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

private:
    void allocate(std::size_t bucket_count);

    std::vector<TTBucket> table_;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 0;
    mutable Stats stats_{};
};

}  // namespace chess
