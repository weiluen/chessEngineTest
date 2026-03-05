#include "chess/transposition_table.hpp"

#include <algorithm>
#include <cstring>

namespace chess {

TranspositionTable::TranspositionTable(std::size_t megabytes) {
    resize(megabytes);
}

void TranspositionTable::allocate(std::size_t bucket_count) {
    std::size_t capacity = 1ULL;
    while (capacity < bucket_count) {
        capacity <<= 1U;
    }
    table_.clear();
    table_.resize(capacity);
    mask_ = capacity - 1ULL;
}

void TranspositionTable::resize(std::size_t megabytes) {
    const std::size_t min_mb = 1;
    const std::size_t bytes = std::max(megabytes, min_mb) * 1024ULL * 1024ULL;
    const std::size_t entry_bytes = sizeof(TTBucket);
    const std::size_t bucket_count = std::max<std::size_t>(1ULL, bytes / entry_bytes);
    allocate(bucket_count);
    clear();
}

void TranspositionTable::clear() {
    std::memset(table_.data(), 0, table_.size() * sizeof(TTBucket));
    generation_ = 0;
    reset_stats();
}

void TranspositionTable::new_search() {
    generation_ = static_cast<std::uint8_t>((generation_ + 1) & 0x3FU);
}

void TranspositionTable::prefetch(Key key) const {
#if defined(__GNUC__) || defined(__clang__)
    const TTBucket* bucket = &table_[key & mask_];
    __builtin_prefetch(bucket, 0, 3);
#else
    (void)key;
#endif
}

void TranspositionTable::store(Key key, int depth, int score, Bound bound, const Move& move) {
    TTBucket& bucket = table_[key & mask_];
    const std::uint32_t key32 = static_cast<std::uint32_t>(key >> 32);
    TTEntry* replace = nullptr;

    for (auto& entry : bucket.entries) {
        if (entry.key32 == key32) {
            replace = &entry;
            break;
        }
        if (replace == nullptr) {
            replace = &entry;
        } else {
            bool entry_empty = entry.is_empty();
            bool replace_empty = replace->is_empty();
            if (entry_empty && !replace_empty) {
                replace = &entry;
            } else if (!entry_empty) {
                if (entry.generation() != generation_ && replace->generation() == generation_) {
                    replace = &entry;
                } else if (entry.generation() == replace->generation()) {
                    if (entry.depth < replace->depth) {
                        replace = &entry;
                    }
                }
            }
        }
    }

    if (replace == nullptr) {
        replace = &bucket.entries.front();
    }

    if (!replace->is_empty() && replace->key32 != key32) {
        ++stats_.replacements;
    }

    if (replace->key32 == key32 && replace->generation() == generation_ && replace->depth > depth) {
        return;
    }

    replace->key32 = key32;
    replace->set_best_move(move);
    replace->score = static_cast<std::int16_t>(std::clamp(score, -InfiniteScore, InfiniteScore));
    replace->depth = static_cast<std::int16_t>(depth);
    replace->set_bound_gen(bound, generation_);
    ++stats_.stores;
}

bool TranspositionTable::probe(Key key, TTEntry& out) const {
    ++stats_.lookups;
    const std::uint32_t key32 = static_cast<std::uint32_t>(key >> 32);
    const TTBucket& bucket = table_[key & mask_];
    for (const auto& entry : bucket.entries) {
        if (entry.key32 == key32 && entry.bound() != Bound::None) {
            out = entry;
            ++stats_.hits;
            return true;
        }
    }
    return false;
}

}  // namespace chess
