#include "chess/position.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace chess {

namespace {

inline int file_of(Square sq) {
    return static_cast<int>(sq) % 8;
}

inline int rank_of(Square sq) {
    return static_cast<int>(sq) / 8;
}

inline Square make_square(int file, int rank) {
    return static_cast<Square>(rank * 8 + file);
}

inline bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

inline Square pop_lsb(Bitboard& bb) {
    Square sq = static_cast<Square>(lsb(bb));
    bb &= bb - 1;
    return sq;
}

Piece char_to_piece(char c) {
    switch (std::tolower(c)) {
        case 'p': return Piece::Pawn;
        case 'n': return Piece::Knight;
        case 'b': return Piece::Bishop;
        case 'r': return Piece::Rook;
        case 'q': return Piece::Queen;
        case 'k': return Piece::King;
        default: throw std::invalid_argument("Invalid piece character in FEN");
    }
}

char piece_to_char(Piece piece, Color color) {
    switch (piece) {
        case Piece::Pawn: return color == Color::White ? 'P' : 'p';
        case Piece::Knight: return color == Color::White ? 'N' : 'n';
        case Piece::Bishop: return color == Color::White ? 'B' : 'b';
        case Piece::Rook: return color == Color::White ? 'R' : 'r';
        case Piece::Queen: return color == Color::White ? 'Q' : 'q';
        case Piece::King: return color == Color::White ? 'K' : 'k';
        default: return '.';
    }
}

constexpr std::uint8_t WK_MASK = static_cast<std::uint8_t>(Castling::WK);
constexpr std::uint8_t WQ_MASK = static_cast<std::uint8_t>(Castling::WQ);
constexpr std::uint8_t BK_MASK = static_cast<std::uint8_t>(Castling::BK);
constexpr std::uint8_t BQ_MASK = static_cast<std::uint8_t>(Castling::BQ);

bool zobrist_ready = false;
Key piece_keys[2][6][64];
Key castle_keys[4];
Key ep_keys[8];
Key side_key = 0;

Key random_key() {
    static Key seed = 0x9E3779B97F4A7C15ULL;
    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;
    return seed * 2685821657736338717ULL;
}

void init_zobrist() {
    if (zobrist_ready) {
        return;
    }
    for (int c = 0; c < 2; ++c) {
        for (int p = 0; p < 6; ++p) {
            for (int sq = 0; sq < 64; ++sq) {
                piece_keys[c][p][sq] = random_key();
            }
        }
    }
    for (auto& key : castle_keys) {
        key = random_key();
    }
    for (auto& key : ep_keys) {
        key = random_key();
    }
    side_key = random_key();
    zobrist_ready = true;
}

void add_move(std::vector<Move>& moves, Square from, Square to, Piece piece,
              Piece capture, Piece promotion, std::uint8_t flags) {
    moves.push_back(Move{
        .from = static_cast<std::uint16_t>(from),
        .to = static_cast<std::uint16_t>(to),
        .piece = piece,
        .capture = capture,
        .promotion = promotion,
        .flags = flags
    });
}

}  // namespace

Position::Position() {
    clear();
}

void Position::clear() {
    init_zobrist();

    for (auto& color : pieces_) {
        color.fill(0ULL);
    }
    occupancy_.fill(0ULL);
    side_to_move_ = Color::White;
    ply_ = 0;
    state_stack_.fill(StateInfo{});
    state_stack_[0].last_move = make_null_move();
    state_stack_[0].captured = Piece::None;
    state_stack_[0].fifty_move_counter = 0;
    state_stack_[0].ply_from_null = 0;
    state_stack_[0].ep_square = SquareNone;
    state_stack_[0].castling_rights = 0;
    state_stack_[0].zobrist = 0ULL;
}

void Position::push_state() {
    state_stack_[ply_ + 1] = state_stack_[ply_];
    ++ply_;
}

void Position::pop_state() {
    if (ply_ > 0) {
        --ply_;
    }
}

void Position::add_piece(Color color, Piece piece, Square sq) {
    if (piece == Piece::None) {
        return;
    }
    const int c = static_cast<int>(color);
    const int p = static_cast<int>(piece);
    Bitboard mask = bit(sq);
    pieces_[c][p] |= mask;
    occupancy_[c] |= mask;
    state_stack_[ply_].zobrist ^= piece_keys[c][p][static_cast<int>(sq)];
}

void Position::remove_piece(Color color, Piece piece, Square sq) {
    if (piece == Piece::None) {
        return;
    }
    const int c = static_cast<int>(color);
    const int p = static_cast<int>(piece);
    Bitboard mask = bit(sq);
    pieces_[c][p] &= ~mask;
    occupancy_[c] &= ~mask;
    state_stack_[ply_].zobrist ^= piece_keys[c][p][static_cast<int>(sq)];
}

Piece Position::piece_on(Color color, Square sq) const {
    const int c = static_cast<int>(color);
    Bitboard mask = bit(sq);
    for (int p = 0; p < 6; ++p) {
        if (pieces_[c][p] & mask) {
            return static_cast<Piece>(p);
        }
    }
    return Piece::None;
}

bool Position::is_square_attacked(Color attacker, Square sq) const {
    const int att = static_cast<int>(attacker);
    Bitboard occ = occupancy_[0] | occupancy_[1];

    if (pawn_attacks(opposite(attacker), sq) & pieces_[att][static_cast<int>(Piece::Pawn)]) {
        return true;
    }
    if (knight_attacks(sq) & pieces_[att][static_cast<int>(Piece::Knight)]) {
        return true;
    }
    if (bishop_attacks(sq, occ) &
        (pieces_[att][static_cast<int>(Piece::Bishop)] | pieces_[att][static_cast<int>(Piece::Queen)])) {
        return true;
    }
    if (rook_attacks(sq, occ) &
        (pieces_[att][static_cast<int>(Piece::Rook)] | pieces_[att][static_cast<int>(Piece::Queen)])) {
        return true;
    }
    if (king_attacks(sq) & pieces_[att][static_cast<int>(Piece::King)]) {
        return true;
    }
    return false;
}

void Position::set_fen(const std::string& fen) {
    clear();

    std::istringstream ss(fen);
    std::string board_part, stm_part, castling_part, ep_part;
    int halfmove = 0;
    int fullmove = 1;

    ss >> board_part >> stm_part >> castling_part >> ep_part >> halfmove >> fullmove;
    if (board_part.empty() || stm_part.empty() || castling_part.empty() || ep_part.empty()) {
        throw std::invalid_argument("Invalid FEN: missing fields");
    }

    int rank = 7;
    int file = 0;
    for (char c : board_part) {
        if (c == '/') {
            --rank;
            file = 0;
            continue;
        }
        if (std::isdigit(c)) {
            file += c - '0';
            continue;
        }
        Color color = std::isupper(c) ? Color::White : Color::Black;
        Piece piece = char_to_piece(c);
        Square sq = make_square(file, rank);
        add_piece(color, piece, sq);
        ++file;
    }

    if (stm_part == "w") {
        side_to_move_ = Color::White;
        state_stack_[ply_].zobrist ^= side_key;
    } else if (stm_part == "b") {
        side_to_move_ = Color::Black;
    } else {
        throw std::invalid_argument("Invalid FEN: side to move");
    }

    std::uint8_t rights = 0;
    if (castling_part != "-") {
        for (char c : castling_part) {
            switch (c) {
                case 'K': rights |= WK_MASK; break;
                case 'Q': rights |= WQ_MASK; break;
                case 'k': rights |= BK_MASK; break;
                case 'q': rights |= BQ_MASK; break;
                default: break;
            }
        }
    }
    state_stack_[ply_].castling_rights = rights;
    if (rights & WK_MASK) state_stack_[ply_].zobrist ^= castle_keys[0];
    if (rights & WQ_MASK) state_stack_[ply_].zobrist ^= castle_keys[1];
    if (rights & BK_MASK) state_stack_[ply_].zobrist ^= castle_keys[2];
    if (rights & BQ_MASK) state_stack_[ply_].zobrist ^= castle_keys[3];

    if (ep_part != "-") {
        int ep_file = ep_part[0] - 'a';
        int ep_rank = ep_part[1] - '1';
        if (on_board(ep_file, ep_rank)) {
            state_stack_[ply_].ep_square = make_square(ep_file, ep_rank);
            state_stack_[ply_].zobrist ^= ep_keys[ep_file];
        }
    } else {
        state_stack_[ply_].ep_square = SquareNone;
    }

    state_stack_[ply_].fifty_move_counter = halfmove;
    state_stack_[ply_].last_move = make_null_move();
    state_stack_[ply_].captured = Piece::None;
    state_stack_[ply_].ply_from_null = 0;
}

std::string Position::fen() const {
    std::ostringstream oss;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            Piece piece = Piece::None;
            Color color = Color::NoColor;
            Bitboard mask = bit(sq);
            for (int c = 0; c < 2; ++c) {
                for (int p = 0; p < 6; ++p) {
                    if (pieces_[c][p] & mask) {
                        piece = static_cast<Piece>(p);
                        color = static_cast<Color>(c);
                        break;
                    }
                }
                if (piece != Piece::None) break;
            }
            if (piece == Piece::None) {
                ++empty;
            } else {
                if (empty != 0) {
                    oss << empty;
                    empty = 0;
                }
                oss << piece_to_char(piece, color);
            }
        }
        if (empty != 0) {
            oss << empty;
        }
        if (rank != 0) {
            oss << '/';
        }
    }

    oss << ' ' << (side_to_move_ == Color::White ? 'w' : 'b') << ' ';

    const auto rights = state_stack_[ply_].castling_rights;
    if (rights == 0) {
        oss << '-';
    } else {
        if (rights & WK_MASK) oss << 'K';
        if (rights & WQ_MASK) oss << 'Q';
        if (rights & BK_MASK) oss << 'k';
        if (rights & BQ_MASK) oss << 'q';
    }

    oss << ' ';
    if (state_stack_[ply_].ep_square != SquareNone) {
        char file_char = 'a' + file_of(state_stack_[ply_].ep_square);
        char rank_char = '1' + rank_of(state_stack_[ply_].ep_square);
        oss << file_char << rank_char;
    } else {
        oss << '-';
    }

    oss << ' ' << state_stack_[ply_].fifty_move_counter << ' ' << 1;
    return oss.str();
}

bool Position::make_null_move() {
    push_state();
    StateInfo& st = state_stack_[ply_];
    if (st.ep_square != SquareNone) {
        st.zobrist ^= ep_keys[file_of(st.ep_square)];
    }
    st.last_move = make_null_move();
    st.captured = Piece::None;
    st.ep_square = SquareNone;
    st.fifty_move_counter += 1;
    st.ply_from_null = 0;
    st.zobrist ^= side_key;
    side_to_move_ = opposite(side_to_move_);
    return true;
}

void Position::unmake_null_move() {
    side_to_move_ = opposite(side_to_move_);
    pop_state();
}

bool Position::make_move(Move move) {
    Color us = side_to_move_;
    Color them = opposite(us);
    const int us_idx = static_cast<int>(us);
    const int them_idx = static_cast<int>(them);

    Square from = static_cast<Square>(move.from);
    Square to = static_cast<Square>(move.to);

    if (from == SquareNone || to == SquareNone) {
        return false;
    }

    Piece moving_piece = piece_on(us, from);
    if (moving_piece == Piece::None) {
        return false;
    }

    Square capture_square = to;
    if ((move.flags & MoveFlagEnPassant) != 0) {
        capture_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
    }

    Piece captured_piece = Piece::None;
    if ((move.flags & MoveFlagCapture) != 0) {
        captured_piece = piece_on(them, capture_square);
        if (captured_piece == Piece::None) {
            return false;
        }
    }

    push_state();
    StateInfo& st = state_stack_[ply_];
    if (st.ep_square != SquareNone) {
        st.zobrist ^= ep_keys[file_of(st.ep_square)];
    }
    st.ep_square = SquareNone;
    st.last_move = move;
    st.captured = captured_piece;
    st.fifty_move_counter += 1;
    st.ply_from_null = state_stack_[ply_ - 1].ply_from_null + 1;

    remove_piece(us, moving_piece, from);

    if ((move.flags & MoveFlagCapture) != 0) {
        remove_piece(them, captured_piece, capture_square);
        st.fifty_move_counter = 0;
    }

    if (moving_piece == Piece::Pawn) {
        st.fifty_move_counter = 0;
    }

    if ((move.flags & MoveFlagPromotion) != 0) {
        add_piece(us, move.promotion, to);
    } else {
        add_piece(us, moving_piece, to);
    }

    if ((move.flags & MoveFlagCastling) != 0) {
        if (to == G1) {
            remove_piece(Color::White, Piece::Rook, H1);
            add_piece(Color::White, Piece::Rook, F1);
        } else if (to == C1) {
            remove_piece(Color::White, Piece::Rook, A1);
            add_piece(Color::White, Piece::Rook, D1);
        } else if (to == G8) {
            remove_piece(Color::Black, Piece::Rook, H8);
            add_piece(Color::Black, Piece::Rook, F8);
        } else if (to == C8) {
            remove_piece(Color::Black, Piece::Rook, A8);
            add_piece(Color::Black, Piece::Rook, D8);
        }
    }

    std::uint8_t old_rights = st.castling_rights;
    if (moving_piece == Piece::King) {
        if (us == Color::White) {
            st.castling_rights &= static_cast<std::uint8_t>(~(WK_MASK | WQ_MASK));
        } else {
            st.castling_rights &= static_cast<std::uint8_t>(~(BK_MASK | BQ_MASK));
        }
    }
    if (moving_piece == Piece::Rook) {
        if (us == Color::White) {
            if (from == H1) st.castling_rights &= static_cast<std::uint8_t>(~WK_MASK);
            if (from == A1) st.castling_rights &= static_cast<std::uint8_t>(~WQ_MASK);
        } else {
            if (from == H8) st.castling_rights &= static_cast<std::uint8_t>(~BK_MASK);
            if (from == A8) st.castling_rights &= static_cast<std::uint8_t>(~BQ_MASK);
        }
    }
    if (captured_piece == Piece::Rook) {
        if (capture_square == H1) st.castling_rights &= static_cast<std::uint8_t>(~WK_MASK);
        if (capture_square == A1) st.castling_rights &= static_cast<std::uint8_t>(~WQ_MASK);
        if (capture_square == H8) st.castling_rights &= static_cast<std::uint8_t>(~BK_MASK);
        if (capture_square == A8) st.castling_rights &= static_cast<std::uint8_t>(~BQ_MASK);
    }

    if ((move.flags & MoveFlagDoublePawn) != 0) {
        int ep_rank = rank_of(from) + (us == Color::White ? 1 : -1);
        st.ep_square = make_square(file_of(from), ep_rank);
        st.zobrist ^= ep_keys[file_of(st.ep_square)];
    }

    if (old_rights != st.castling_rights) {
        std::uint8_t diff = static_cast<std::uint8_t>(old_rights ^ st.castling_rights);
        if (diff & WK_MASK) st.zobrist ^= castle_keys[0];
        if (diff & WQ_MASK) st.zobrist ^= castle_keys[1];
        if (diff & BK_MASK) st.zobrist ^= castle_keys[2];
        if (diff & BQ_MASK) st.zobrist ^= castle_keys[3];
    }

    side_to_move_ = them;
    st.zobrist ^= side_key;

    Bitboard king_bb = pieces_[us_idx][static_cast<int>(Piece::King)];
    if (!king_bb) {
        unmake_move();
        return false;
    }
    Square king_sq = static_cast<Square>(lsb(king_bb));
    if (is_square_attacked(them, king_sq)) {
        unmake_move();
        return false;
    }

    return true;
}

void Position::unmake_move() {
    const StateInfo& st = state_stack_[ply_];
    Move move = st.last_move;
    Color them = side_to_move_;
    Color us = opposite(them);

    Square from = static_cast<Square>(move.from);
    Square to = static_cast<Square>(move.to);

    Square capture_square = to;
    if ((move.flags & MoveFlagEnPassant) != 0) {
        capture_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
    }

    side_to_move_ = us;

    if ((move.flags & MoveFlagCastling) != 0) {
        if (to == G1) {
            remove_piece(Color::White, Piece::Rook, F1);
            add_piece(Color::White, Piece::Rook, H1);
        } else if (to == C1) {
            remove_piece(Color::White, Piece::Rook, D1);
            add_piece(Color::White, Piece::Rook, A1);
        } else if (to == G8) {
            remove_piece(Color::Black, Piece::Rook, F8);
            add_piece(Color::Black, Piece::Rook, H8);
        } else if (to == C8) {
            remove_piece(Color::Black, Piece::Rook, D8);
            add_piece(Color::Black, Piece::Rook, A8);
        }
    }

    if ((move.flags & MoveFlagPromotion) != 0) {
        remove_piece(us, move.promotion, to);
        add_piece(us, Piece::Pawn, from);
    } else {
        remove_piece(us, move.piece, to);
        add_piece(us, move.piece, from);
    }

    if (st.captured != Piece::None) {
        add_piece(opposite(us), st.captured, capture_square);
    }

    pop_state();
}

bool Position::in_check(Color side) const {
    Bitboard king_bb = pieces_[static_cast<int>(side)][static_cast<int>(Piece::King)];
    if (!king_bb) {
        return false;
    }
    Square king_sq = static_cast<Square>(lsb(king_bb));
    return is_square_attacked(opposite(side), king_sq);
}

int Position::material_balance() const {
    static constexpr int piece_values[6] = {100, 320, 330, 500, 900, 20000};
    int score = 0;
    for (int p = 0; p < 6; ++p) {
        score += popcount(pieces_[static_cast<int>(Color::White)][p]) * piece_values[p];
        score -= popcount(pieces_[static_cast<int>(Color::Black)][p]) * piece_values[p];
    }
    return score;
}

std::vector<Move> Position::generate_legal_moves() const {
    std::vector<Move> pseudo;
    pseudo.reserve(64);

    Color us = side_to_move_;
    Color them = opposite(us);
    const int us_idx = static_cast<int>(us);
    const StateInfo& st = state_stack_[ply_];
    Bitboard occ = occupancy_[0] | occupancy_[1];
    Bitboard enemy_occ = occupancy_[static_cast<int>(them)];

    Bitboard pawns = pieces_[us_idx][static_cast<int>(Piece::Pawn)];
    int forward = us == Color::White ? 8 : -8;
    int start_rank = us == Color::White ? 1 : 6;
    int promotion_rank = us == Color::White ? 7 : 0;
    while (pawns) {
        Square from = pop_lsb(pawns);
        int from_idx = static_cast<int>(from);
        int to_idx = from_idx + forward;
        if (to_idx >= 0 && to_idx < 64) {
            Square to = static_cast<Square>(to_idx);
            Bitboard to_bb = bit(to);
            if (!(occ & to_bb)) {
                if (rank_of(to) == promotion_rank) {
                    add_move(pseudo, from, to, Piece::Pawn, Piece::None, Piece::Queen, MoveFlagPromotion);
                    add_move(pseudo, from, to, Piece::Pawn, Piece::None, Piece::Rook, MoveFlagPromotion);
                    add_move(pseudo, from, to, Piece::Pawn, Piece::None, Piece::Bishop, MoveFlagPromotion);
                    add_move(pseudo, from, to, Piece::Pawn, Piece::None, Piece::Knight, MoveFlagPromotion);
                } else {
                    add_move(pseudo, from, to, Piece::Pawn, Piece::None, Piece::None, MoveFlagQuiet);
                    if (rank_of(from) == start_rank) {
                        int double_idx = from_idx + 2 * forward;
                        Square double_to = static_cast<Square>(double_idx);
                        Bitboard between_bb = bit(static_cast<Square>(from_idx + forward));
                        if (!(occ & bit(double_to)) && !(occ & between_bb)) {
                            add_move(pseudo, from, double_to, Piece::Pawn, Piece::None, Piece::None,
                                     static_cast<std::uint8_t>(MoveFlagQuiet | MoveFlagDoublePawn));
                        }
                    }
                }
            }
        }

        Bitboard attacks = pawn_attacks(us, from);
        Bitboard capture_targets = attacks & enemy_occ;
        while (capture_targets) {
            Square to = pop_lsb(capture_targets);
            Piece captured = piece_on(them, to);
            std::uint8_t flags = MoveFlagCapture;
            if (rank_of(to) == promotion_rank) {
                flags |= MoveFlagPromotion;
                add_move(pseudo, from, to, Piece::Pawn, captured, Piece::Queen, flags);
                add_move(pseudo, from, to, Piece::Pawn, captured, Piece::Rook, flags);
                add_move(pseudo, from, to, Piece::Pawn, captured, Piece::Bishop, flags);
                add_move(pseudo, from, to, Piece::Pawn, captured, Piece::Knight, flags);
            } else {
                add_move(pseudo, from, to, Piece::Pawn, captured, Piece::None, flags);
            }
        }

        if (st.ep_square != SquareNone) {
            Bitboard ep_mask = bit(st.ep_square);
            if (attacks & ep_mask) {
                add_move(pseudo, from, st.ep_square, Piece::Pawn, Piece::Pawn,
                         Piece::None, static_cast<std::uint8_t>(MoveFlagEnPassant | MoveFlagCapture));
            }
        }
    }

    auto generate_piece_moves = [&](Piece piece, Bitboard mask, auto attack_fn) {
        while (mask) {
            Square from = pop_lsb(mask);
            Bitboard occ_without_from = occ & ~bit(from);
            Bitboard attacks = attack_fn(from, occ_without_from);
            Bitboard quiet = attacks & ~occ;
            Bitboard captures = attacks & enemy_occ;
            while (quiet) {
                Square to = pop_lsb(quiet);
                add_move(pseudo, from, to, piece, Piece::None, Piece::None, MoveFlagQuiet);
            }
            while (captures) {
                Square to = pop_lsb(captures);
                Piece captured = piece_on(them, to);
                add_move(pseudo, from, to, piece, captured, Piece::None, MoveFlagCapture);
            }
        }
    };

    generate_piece_moves(Piece::Knight, pieces_[us_idx][static_cast<int>(Piece::Knight)],
                         [](Square sq, Bitboard) { return knight_attacks(sq); });
    generate_piece_moves(Piece::Bishop, pieces_[us_idx][static_cast<int>(Piece::Bishop)],
                         [](Square sq, Bitboard occ_bb) { return bishop_attacks(sq, occ_bb); });
    generate_piece_moves(Piece::Rook, pieces_[us_idx][static_cast<int>(Piece::Rook)],
                         [](Square sq, Bitboard occ_bb) { return rook_attacks(sq, occ_bb); });
    generate_piece_moves(Piece::Queen, pieces_[us_idx][static_cast<int>(Piece::Queen)],
                         [](Square sq, Bitboard occ_bb) { return bishop_attacks(sq, occ_bb) | rook_attacks(sq, occ_bb); });
    generate_piece_moves(Piece::King, pieces_[us_idx][static_cast<int>(Piece::King)],
                         [](Square sq, Bitboard) { return king_attacks(sq); });

    const auto rights = st.castling_rights;
    if (us == Color::White) {
        if ((rights & WK_MASK) &&
            !(occ & (bit(F1) | bit(G1))) &&
            !is_square_attacked(them, E1) &&
            !is_square_attacked(them, F1) &&
            !is_square_attacked(them, G1)) {
            add_move(pseudo, E1, G1, Piece::King, Piece::None, Piece::None, MoveFlagCastling);
        }
        if ((rights & WQ_MASK) &&
            !(occ & (bit(B1) | bit(C1) | bit(D1))) &&
            !is_square_attacked(them, E1) &&
            !is_square_attacked(them, D1) &&
            !is_square_attacked(them, C1)) {
            add_move(pseudo, E1, C1, Piece::King, Piece::None, Piece::None, MoveFlagCastling);
        }
    } else {
        if ((rights & BK_MASK) &&
            !(occ & (bit(F8) | bit(G8))) &&
            !is_square_attacked(them, E8) &&
            !is_square_attacked(them, F8) &&
            !is_square_attacked(them, G8)) {
            add_move(pseudo, E8, G8, Piece::King, Piece::None, Piece::None, MoveFlagCastling);
        }
        if ((rights & BQ_MASK) &&
            !(occ & (bit(B8) | bit(C8) | bit(D8))) &&
            !is_square_attacked(them, E8) &&
            !is_square_attacked(them, D8) &&
            !is_square_attacked(them, C8)) {
            add_move(pseudo, E8, C8, Piece::King, Piece::None, Piece::None, MoveFlagCastling);
        }
    }

    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    Position copy = *this;
    for (const Move& move : pseudo) {
        if (copy.make_move(move)) {
            Move legal_move = move;
            if (copy.in_check(copy.side_to_move())) {
                legal_move.flags |= MoveFlagCheck;
            }
            copy.unmake_move();
            legal.push_back(legal_move);
        }
    }

    return legal;
}

Key Position::compute_zobrist() const {
    init_zobrist();
    Key key = 0ULL;
    for (int c = 0; c < 2; ++c) {
        for (int p = 0; p < 6; ++p) {
            Bitboard bb = pieces_[c][p];
            while (bb) {
                Square sq = pop_lsb(bb);
                key ^= piece_keys[c][p][static_cast<int>(sq)];
            }
        }
    }

    if (side_to_move_ == Color::White) {
        key ^= side_key;
    }

    const auto rights = state_stack_[ply_].castling_rights;
    if (rights & WK_MASK) key ^= castle_keys[0];
    if (rights & WQ_MASK) key ^= castle_keys[1];
    if (rights & BK_MASK) key ^= castle_keys[2];
    if (rights & BQ_MASK) key ^= castle_keys[3];

    if (state_stack_[ply_].ep_square != SquareNone) {
        key ^= ep_keys[file_of(state_stack_[ply_].ep_square)];
    }

    return key;
}

}  // namespace chess
