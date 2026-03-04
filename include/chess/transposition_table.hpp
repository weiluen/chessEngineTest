/**
 * Lock-free transposition table tuned for cache-friendly probing.
 */
#pragma once

#include "move.hpp"
#include "types.hpp"

#include <atomic>
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

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    void resize(std::size_t megabytes);
    void clear();
    void new_search();

    void store(Key key, int depth, int score, Bound bound, Move move);
    [[nodiscard]] bool probe(Key key, TTEntry& entry) const;

private:
    void allocate(std::size_t size);

    std::vector<TTEntry> table_;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 0;
};

}  // namespace chess
