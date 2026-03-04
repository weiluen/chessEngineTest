#include "chess/bitboard.hpp"

#include <array>
#include <cstdlib>

namespace chess {

namespace {

std::array<Bitboard, 64> knight_table{};
std::array<Bitboard, 64> king_table{};
std::array<Bitboard, 64> pawn_table_white{};
std::array<Bitboard, 64> pawn_table_black{};

// --- Magic bitboard infrastructure ---

struct MagicEntry {
    Bitboard* attacks;
    Bitboard mask;
    Bitboard magic;
    int shift;
};

MagicEntry bishop_magics[64];
MagicEntry rook_magics[64];

// Larger flat tables to accommodate runtime-found magics
// Max possible: bishop needs up to 512 per square (9 bits), rook up to 4096 (12 bits)
// Total bishop: 64 * 512 = 32768, total rook: 64 * 4096 = 262144
Bitboard bishop_attack_table[0x1480];  // 5248 entries (exact sum of 2^bits per square)
Bitboard rook_attack_table[0x19000];   // 102400 entries

constexpr bool on_board(int file, int rank) noexcept {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

Bitboard slow_bishop_attacks(int sq, Bitboard occupied) noexcept {
    Bitboard attacks = 0ULL;
    const int file = sq % 8;
    const int rank = sq / 8;
    constexpr int deltas[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (auto [df, dr] : deltas) {
        int nf = file + df;
        int nr = rank + dr;
        while (on_board(nf, nr)) {
            Square target = static_cast<Square>(nr * 8 + nf);
            attacks |= bit(target);
            if (occupied & bit(target)) {
                break;
            }
            nf += df;
            nr += dr;
        }
    }
    return attacks;
}

Bitboard slow_rook_attacks(int sq, Bitboard occupied) noexcept {
    Bitboard attacks = 0ULL;
    const int file = sq % 8;
    const int rank = sq / 8;
    constexpr int deltas[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto [df, dr] : deltas) {
        int nf = file + df;
        int nr = rank + dr;
        while (on_board(nf, nr)) {
            Square target = static_cast<Square>(nr * 8 + nf);
            attacks |= bit(target);
            if (occupied & bit(target)) {
                break;
            }
            nf += df;
            nr += dr;
        }
    }
    return attacks;
}

Bitboard bishop_mask(int sq) {
    Bitboard mask = 0ULL;
    int file = sq % 8;
    int rank = sq / 8;
    constexpr int deltas[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (auto [df, dr] : deltas) {
        int nf = file + df;
        int nr = rank + dr;
        while (nf > 0 && nf < 7 && nr > 0 && nr < 7) {
            mask |= Bitboard(1) << (nr * 8 + nf);
            nf += df;
            nr += dr;
        }
    }
    return mask;
}

Bitboard rook_mask(int sq) {
    Bitboard mask = 0ULL;
    int file = sq % 8;
    int rank = sq / 8;
    for (int f = file + 1; f < 7; ++f) mask |= Bitboard(1) << (rank * 8 + f);
    for (int f = file - 1; f > 0; --f) mask |= Bitboard(1) << (rank * 8 + f);
    for (int r = rank + 1; r < 7; ++r) mask |= Bitboard(1) << (r * 8 + file);
    for (int r = rank - 1; r > 0; --r) mask |= Bitboard(1) << (r * 8 + file);
    return mask;
}

// Enumerate all occupancy subsets for a given mask
void enumerate_occupancies(Bitboard mask, Bitboard* occs, Bitboard* attacks, int sq, bool is_bishop) {
    int bits = popcount(mask);
    int num = 1 << bits;
    for (int i = 0; i < num; ++i) {
        Bitboard occ = 0;
        Bitboard mask_copy = mask;
        for (int j = 0; j < bits; ++j) {
            int bit_idx = __builtin_ctzll(mask_copy);
            mask_copy &= mask_copy - 1;
            if (i & (1 << j)) {
                occ |= Bitboard(1) << bit_idx;
            }
        }
        occs[i] = occ;
        attacks[i] = is_bishop ? slow_bishop_attacks(sq, occ) : slow_rook_attacks(sq, occ);
    }
}

// PRNG for magic number search
Bitboard magic_rng_state = 1070372ULL;

Bitboard random_sparse_u64() {
    // xorshift64
    magic_rng_state ^= magic_rng_state >> 12;
    magic_rng_state ^= magic_rng_state << 25;
    magic_rng_state ^= magic_rng_state >> 27;
    Bitboard r1 = magic_rng_state * 2685821657736338717ULL;
    magic_rng_state ^= magic_rng_state >> 12;
    magic_rng_state ^= magic_rng_state << 25;
    magic_rng_state ^= magic_rng_state >> 27;
    Bitboard r2 = magic_rng_state * 2685821657736338717ULL;
    magic_rng_state ^= magic_rng_state >> 12;
    magic_rng_state ^= magic_rng_state << 25;
    magic_rng_state ^= magic_rng_state >> 27;
    Bitboard r3 = magic_rng_state * 2685821657736338717ULL;
    return r1 & r2 & r3;  // sparse number (few bits set)
}

Bitboard find_magic(int sq, int bits, Bitboard mask, bool is_bishop) {
    int num = 1 << bits;
    Bitboard occs[4096];
    Bitboard atks[4096];
    Bitboard used[4096];

    enumerate_occupancies(mask, occs, atks, sq, is_bishop);

    for (int attempt = 0; attempt < 100000000; ++attempt) {
        Bitboard magic = random_sparse_u64();
        // Quick check: magic * mask should have enough bits in upper part
        if (popcount((mask * magic) & 0xFF00000000000000ULL) < 6) continue;

        for (int i = 0; i < num; ++i) used[i] = 0;

        bool fail = false;
        for (int i = 0; i < num; ++i) {
            int index = static_cast<int>((occs[i] * magic) >> (64 - bits));
            if (used[index] == 0) {
                used[index] = atks[i];
            } else if (used[index] != atks[i]) {
                fail = true;
                break;
            }
        }
        if (!fail) return magic;
    }
    // Should never reach here with reasonable bit counts
    std::abort();
}

void init_bishop_magics() {
    Bitboard* table_ptr = bishop_attack_table;
    for (int sq = 0; sq < 64; ++sq) {
        MagicEntry& entry = bishop_magics[sq];
        entry.mask = bishop_mask(sq);
        int bits = popcount(entry.mask);
        entry.shift = 64 - bits;
        entry.attacks = table_ptr;
        entry.magic = find_magic(sq, bits, entry.mask, true);

        int num_occupancies = 1 << bits;

        // Populate attack table with found magic
        for (int i = 0; i < num_occupancies; ++i) {
            Bitboard occ = 0;
            Bitboard mask_copy = entry.mask;
            int b = bits;
            for (int j = 0; j < b; ++j) {
                int bit_idx = __builtin_ctzll(mask_copy);
                mask_copy &= mask_copy - 1;
                if (i & (1 << j)) {
                    occ |= Bitboard(1) << bit_idx;
                }
            }
            int index = static_cast<int>((occ * entry.magic) >> entry.shift);
            entry.attacks[index] = slow_bishop_attacks(sq, occ);
        }

        table_ptr += num_occupancies;
    }
}

void init_rook_magics() {
    Bitboard* table_ptr = rook_attack_table;
    for (int sq = 0; sq < 64; ++sq) {
        MagicEntry& entry = rook_magics[sq];
        entry.mask = rook_mask(sq);
        int bits = popcount(entry.mask);
        entry.shift = 64 - bits;
        entry.attacks = table_ptr;
        entry.magic = find_magic(sq, bits, entry.mask, false);

        int num_occupancies = 1 << bits;

        for (int i = 0; i < num_occupancies; ++i) {
            Bitboard occ = 0;
            Bitboard mask_copy = entry.mask;
            int b = bits;
            for (int j = 0; j < b; ++j) {
                int bit_idx = __builtin_ctzll(mask_copy);
                mask_copy &= mask_copy - 1;
                if (i & (1 << j)) {
                    occ |= Bitboard(1) << bit_idx;
                }
            }
            int index = static_cast<int>((occ * entry.magic) >> entry.shift);
            entry.attacks[index] = slow_rook_attacks(sq, occ);
        }

        table_ptr += num_occupancies;
    }
}

// --- End magic bitboard infrastructure ---

void init_knight_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        constexpr int offsets[8][2] = {
            {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
            {1, -2}, {2, -1}, {-1, -2}, {-2, -1}
        };
        for (auto [df, dr] : offsets) {
            int nf = file + df;
            int nr = rank + dr;
            if (on_board(nf, nr)) {
                mask |= bit(static_cast<Square>(nr * 8 + nf));
            }
        }
        knight_table[sq] = mask;
    }
}

void init_king_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) {
                    continue;
                }
                int nf = file + df;
                int nr = rank + dr;
                if (on_board(nf, nr)) {
                    mask |= bit(static_cast<Square>(nr * 8 + nf));
                }
            }
        }
        king_table[sq] = mask;
    }
}

void init_pawn_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask_white = 0ULL;
        Bitboard mask_black = 0ULL;
        int file = sq % 8;
        int rank = sq / 8;
        int nr = rank + 1;
        if (on_board(file + 1, nr)) {
            mask_white |= bit(static_cast<Square>(nr * 8 + (file + 1)));
        }
        if (on_board(file - 1, nr)) {
            mask_white |= bit(static_cast<Square>(nr * 8 + (file - 1)));
        }
        nr = rank - 1;
        if (on_board(file + 1, nr)) {
            mask_black |= bit(static_cast<Square>(nr * 8 + (file + 1)));
        }
        if (on_board(file - 1, nr)) {
            mask_black |= bit(static_cast<Square>(nr * 8 + (file - 1)));
        }
        pawn_table_white[sq] = mask_white;
        pawn_table_black[sq] = mask_black;
    }
}

}  // namespace

void init_attack_tables() {
    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
    init_bishop_magics();
    init_rook_magics();
}

Bitboard knight_attacks(Square sq) noexcept {
    return knight_table[static_cast<int>(sq)];
}

Bitboard king_attacks(Square sq) noexcept {
    return king_table[static_cast<int>(sq)];
}

Bitboard pawn_attacks(Color side, Square sq) noexcept {
    return side == Color::White
               ? pawn_table_white[static_cast<int>(sq)]
               : pawn_table_black[static_cast<int>(sq)];
}

Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
    const MagicEntry& entry = bishop_magics[static_cast<int>(sq)];
    return entry.attacks[((occupied & entry.mask) * entry.magic) >> entry.shift];
}

Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
    const MagicEntry& entry = rook_magics[static_cast<int>(sq)];
    return entry.attacks[((occupied & entry.mask) * entry.magic) >> entry.shift];
}

}  // namespace chess
