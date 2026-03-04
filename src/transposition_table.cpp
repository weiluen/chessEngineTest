#include "chess/transposition_table.hpp"

#include <algorithm>
#include <cstring>

namespace chess {

TranspositionTable::TranspositionTable(std::size_t megabytes) {
    resize(megabytes);
}

void TranspositionTable::allocate(std::size_t size) {
    std::size_t capacity = 1ULL;
    while (capacity < size) {
        capacity <<= 1U;
    }
    table_.clear();
    table_.resize(capacity);
    mask_ = capacity - 1ULL;
}

void TranspositionTable::resize(std::size_t megabytes) {
    const std::size_t min_mb = 1;
    const std::size_t bytes = std::max(megabytes, min_mb) * 1024ULL * 1024ULL;
    const std::size_t entry_count = std::max<std::size_t>(1ULL, bytes / sizeof(TTEntry));
    allocate(entry_count);
    clear();
}

void TranspositionTable::clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTEntry));
    generation_ = 0;
}

void TranspositionTable::new_search() {
    ++generation_;
}

void TranspositionTable::store(Key key, int depth, int score, Bound bound, Move move) {
    TTEntry& entry = table_[key & mask_];
    if (entry.key != key && entry.key != 0) {
        if (depth < entry.depth && entry.generation == generation_) {
            return;
        }
    } else if (entry.key == key && depth < entry.depth) {
        return;
    }

    entry.key = key;
    entry.best_move = move;
    entry.score = static_cast<std::int16_t>(std::clamp(score, -InfiniteScore, InfiniteScore));
    entry.depth = static_cast<std::int16_t>(depth);
    entry.bound = bound;
    entry.generation = generation_;
}

bool TranspositionTable::probe(Key key, TTEntry& out) const {
    const TTEntry& entry = table_[key & mask_];
    if (entry.key == key && entry.bound != Bound::None) {
        out = entry;
        return true;
    }
    return false;
}

}  // namespace chess
