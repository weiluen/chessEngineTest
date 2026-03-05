#include "chess/bitboard.hpp"

#include <array>

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

// Pre-computed magic numbers (deterministic, verified against runtime search)
constexpr Bitboard BishopMagics[64] = {
    0x2008021012002502ULL, 0x04D0100110628400ULL, 0x21102080A1021010ULL, 0x2044041080000400ULL,
    0x0004050402800000ULL, 0x0002010420109560ULL, 0x08040084500A0000ULL, 0x9401002104224008ULL,
    0x40044350070B0100ULL, 0x90B00888088C1040ULL, 0x0100100440444012ULL, 0x80001104008A0940ULL,
    0x1042920210504048ULL, 0x0000010420048200ULL, 0x000000A410221000ULL, 0x804800829C901001ULL,
    0x0040002008010120ULL, 0x8802008424280205ULL, 0x200800010A040010ULL, 0x2420800802004008ULL,
    0x0012011402A21220ULL, 0x2002028508022208ULL, 0x0486200049100802ULL, 0x2000211101080200ULL,
    0x8020200044140C60ULL, 0x0810680C05080381ULL, 0x0001442028012400ULL, 0x4028088008020002ULL,
    0x25C1001041004010ULL, 0x0401020049080140ULL, 0x0004004084210400ULL, 0x40010900104400A0ULL,
    0x011011480004A800ULL, 0x0082020200A0680BULL, 0x0800203000080082ULL, 0x0005020081880080ULL,
    0x1050120080001004ULL, 0x0020008880030810ULL, 0x2241180900008C30ULL, 0x0201451101012400ULL,
    0x8444016008025000ULL, 0x0002080104000800ULL, 0x2801001490090200ULL, 0x0500142018001100ULL,
    0x0300040408200400ULL, 0x0008008800820810ULL, 0x0804210204004212ULL, 0x000800A698800202ULL,
    0x0411040202401000ULL, 0x0A008C051802000EULL, 0x1002A100A8040022ULL, 0x00000C0084042600ULL,
    0x1000884048220000ULL, 0x0082200410208000ULL, 0x0222020441140022ULL, 0x1004080800408810ULL,
    0x0022410801500201ULL, 0x010000410818020BULL, 0x2044000044040410ULL, 0x00200C0100208801ULL,
    0x080800200A102400ULL, 0x000404C010020090ULL, 0x1002101418808C03ULL, 0x0011300081040020ULL,
};

constexpr Bitboard RookMagics[64] = {
    0xA680042040001480ULL, 0x40C0014010002000ULL, 0x0200100820804202ULL, 0x0900100008210004ULL,
    0x4A00108402000820ULL, 0x2200040200018810ULL, 0x03000100220008ACULL, 0x4080002044800D00ULL,
    0x008C800080400820ULL, 0x400240012002D000ULL, 0x0001001041002008ULL, 0x0110801000080080ULL,
    0x0001000500100800ULL, 0x8A46000408020010ULL, 0x00040010084104A2ULL, 0x014A000220804401ULL,
    0x80102A8000400088ULL, 0x0020008020804000ULL, 0x4010008010200081ULL, 0x0208010100100020ULL,
    0x2091010008001005ULL, 0x0002008080020400ULL, 0x240024001110C208ULL, 0x0400120001008054ULL,
    0x8080208080004004ULL, 0x80DD5004C0042000ULL, 0x0410040120080120ULL, 0x2000D00180380080ULL,
    0x0008000880040080ULL, 0x100A000200080410ULL, 0x0300080400100102ULL, 0x6200008200011044ULL,
    0x061481400C800060ULL, 0x1001004001002084ULL, 0x0000200080801000ULL, 0x840010010100200BULL,
    0x0028040080800800ULL, 0x0882000406001830ULL, 0x0001005421001200ULL, 0x000001804600010CULL,
    0x0000804000208000ULL, 0x4400402010044000ULL, 0x4010008020028014ULL, 0x0000090410010020ULL,
    0x0000080100110005ULL, 0x0A00201004080140ULL, 0x0000040200010100ULL, 0x0220007081020004ULL,
    0x840205C981002A00ULL, 0x0000804000200480ULL, 0x0002081040802200ULL, 0x0240230010000900ULL,
    0x0044800800240180ULL, 0x4011000400080300ULL, 0x00101011088A0C00ULL, 0x1003000080420100ULL,
    0x0180102100408001ULL, 0x1100108040010021ULL, 0x0182004008108022ULL, 0x0122900128202501ULL,
    0x0002012004100802ULL, 0x00C200834C081002ULL, 0x0440020110083084ULL, 0x4000484884010022ULL,
};

// Enumerate all occupancy subsets for a given mask
void enumerate_occupancies(Bitboard mask, int sq, bool is_bishop, Bitboard* table_ptr) {
    int bits = popcount(mask);
    int num = 1 << bits;
    Bitboard magic = is_bishop ? BishopMagics[sq] : RookMagics[sq];
    int shift = 64 - bits;
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
        int index = static_cast<int>((occ * magic) >> shift);
        table_ptr[index] = is_bishop ? slow_bishop_attacks(sq, occ) : slow_rook_attacks(sq, occ);
    }
}

void init_bishop_magics() {
    Bitboard* table_ptr = bishop_attack_table;
    for (int sq = 0; sq < 64; ++sq) {
        MagicEntry& entry = bishop_magics[sq];
        entry.mask = bishop_mask(sq);
        int bits = popcount(entry.mask);
        entry.shift = 64 - bits;
        entry.attacks = table_ptr;
        entry.magic = BishopMagics[sq];

        enumerate_occupancies(entry.mask, sq, true, table_ptr);
        table_ptr += (1 << bits);
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
        entry.magic = RookMagics[sq];

        enumerate_occupancies(entry.mask, sq, false, table_ptr);
        table_ptr += (1 << bits);
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
