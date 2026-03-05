/**
 * Lock-free transposition table tuned for cache-friendly probing.
 *
 * TTEntry is packed to exactly 16 bytes so that a 4-entry TTBucket fits
 * one 64-byte cache line, eliminating secondary cache misses on probes.
 */
#pragma once

#include "move.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace chess {

enum class Bound : std::uint8_t { Exact, Lower, Upper, None };

/// Pack a Move into 32 bits: from(6) | to(6) | piece(3) | capture(3) | promotion(3) | flags(6) = 27 bits
inline std::uint32_t pack_move(const Move& m) noexcept {
    return (static_cast<std::uint32_t>(m.from) & 0x3FU)
         | ((static_cast<std::uint32_t>(m.to) & 0x3FU) << 6)
         | ((static_cast<std::uint32_t>(m.piece) & 0x7U) << 12)
         | ((static_cast<std::uint32_t>(m.capture) & 0x7U) << 15)
         | ((static_cast<std::uint32_t>(m.promotion) & 0x7U) << 18)
         | ((static_cast<std::uint32_t>(m.flags) & 0x3FU) << 21);
}

inline Move unpack_move(std::uint32_t v) noexcept {
    Move m;
    m.from       = static_cast<std::uint16_t>(v & 0x3FU);
    m.to         = static_cast<std::uint16_t>((v >> 6) & 0x3FU);
    m.piece      = static_cast<Piece>((v >> 12) & 0x7U);
    m.capture    = static_cast<Piece>((v >> 15) & 0x7U);
    m.promotion  = static_cast<Piece>((v >> 18) & 0x7U);
    m.flags      = static_cast<std::uint8_t>((v >> 21) & 0x3FU);
    return m;
}

/// Compact TT entry: 16 bytes.  4 entries = 64 bytes = one cache line.
struct TTEntry {
    std::uint32_t key32 = 0;       // upper 32 bits of zobrist (lower bits are bucket index)
    std::uint32_t packed_move = 0;  // pack_move() encoding
    std::int16_t  score = 0;
    std::int16_t  depth = -1;
    std::uint8_t  bound_gen = 0;   // bits 0-1: Bound, bits 2-7: generation (6 bits, wraps at 64)
    std::uint8_t  _pad[3] = {};

    [[nodiscard]] Bound bound() const noexcept {
        return static_cast<Bound>(bound_gen & 0x3U);
    }
    [[nodiscard]] std::uint8_t generation() const noexcept {
        return (bound_gen >> 2) & 0x3FU;
    }
    void set_bound_gen(Bound b, std::uint8_t gen) noexcept {
        bound_gen = static_cast<std::uint8_t>((static_cast<std::uint8_t>(b) & 0x3U) | ((gen & 0x3FU) << 2));
    }

    [[nodiscard]] Move best_move() const noexcept { return unpack_move(packed_move); }
    void set_best_move(const Move& m) noexcept { packed_move = pack_move(m); }

    [[nodiscard]] bool is_empty() const noexcept { return key32 == 0 && packed_move == 0; }
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must be exactly 16 bytes");

struct TTBucket {
    static constexpr std::size_t kSize = 4;
    std::array<TTEntry, kSize> entries{};
};

static_assert(sizeof(TTBucket) == 64, "TTBucket must be exactly 64 bytes (one cache line)");

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    void resize(std::size_t megabytes);
    void clear();
    void new_search();

    void store(Key key, int depth, int score, Bound bound, const Move& move);
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
