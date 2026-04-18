#include "position.h"
#include "zobrist.h"
#include <cctype>
#include <cstdio>
#include <sstream>

using BB::sq_bb;

// Per-square mask of rights to preserve when square changes occupancy.
// Start all bits set; clear rights tied to king/rook home squares.
static int CastlingMask[64];

static void init_castling_mask() {
    for (int i = 0; i < 64; ++i) CastlingMask[i] = 0xF;
    CastlingMask[E1] = ~(WHITE_OO | WHITE_OOO) & 0xF;
    CastlingMask[A1] = ~WHITE_OOO & 0xF;
    CastlingMask[H1] = ~WHITE_OO  & 0xF;
    CastlingMask[E8] = ~(BLACK_OO | BLACK_OOO) & 0xF;
    CastlingMask[A8] = ~BLACK_OOO & 0xF;
    CastlingMask[H8] = ~BLACK_OO  & 0xF;
}

struct CastlingMaskInit { CastlingMaskInit() { init_castling_mask(); } };
static CastlingMaskInit _castling_init;

void Position::clear() {
    board_.fill(NO_PIECE);
    type_bb_.fill(0);
    occ_[WHITE] = occ_[BLACK] = 0;
    stm_ = WHITE;
    fullmove_ = 1;
    st_ = StateInfo{0, 0, SQ_NONE, 0, NO_PIECE, 0};
    history_.clear();
    key_history_.clear();
    key_history_root_ = 0;
}

void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    Bitboard bb = sq_bb(s);
    type_bb_[type_of(p)] |= bb;
    occ_[color_of(p)]    |= bb;
    st_.key ^= Zobrist::psq[p][s];
}

void Position::remove_piece(Square s) {
    Piece p = board_[s];
    Bitboard bb = sq_bb(s);
    type_bb_[type_of(p)] ^= bb;
    occ_[color_of(p)]    ^= bb;
    board_[s] = NO_PIECE;
    st_.key ^= Zobrist::psq[p][s];
}

void Position::move_piece(Square from, Square to) {
    Piece p = board_[from];
    Bitboard bb = sq_bb(from) ^ sq_bb(to);
    type_bb_[type_of(p)] ^= bb;
    occ_[color_of(p)]    ^= bb;
    board_[from] = NO_PIECE;
    board_[to]   = p;
    st_.key ^= Zobrist::psq[p][from] ^ Zobrist::psq[p][to];
}

void Position::set_startpos() {
    set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool Position::set_fen(const std::string& fen) {
    clear();
    std::istringstream iss(fen);
    std::string board_str, stm_str, castle_str, ep_str;
    int half = 0, full = 1;
    iss >> board_str >> stm_str >> castle_str >> ep_str;
    iss >> half >> full;

    int rank = 7, file = 0;
    for (char c : board_str) {
        if (c == '/') { --rank; file = 0; continue; }
        if (std::isdigit(c)) { file += c - '0'; continue; }
        Piece p = NO_PIECE;
        switch (c) {
            case 'P': p = W_PAWN; break;
            case 'N': p = W_KNIGHT; break;
            case 'B': p = W_BISHOP; break;
            case 'R': p = W_ROOK; break;
            case 'Q': p = W_QUEEN; break;
            case 'K': p = W_KING; break;
            case 'p': p = B_PAWN; break;
            case 'n': p = B_KNIGHT; break;
            case 'b': p = B_BISHOP; break;
            case 'r': p = B_ROOK; break;
            case 'q': p = B_QUEEN; break;
            case 'k': p = B_KING; break;
            default: return false;
        }
        put_piece(p, Square(rank * 8 + file));
        ++file;
    }

    stm_ = (stm_str == "w") ? WHITE : BLACK;
    if (stm_ == BLACK) st_.key ^= Zobrist::side;

    st_.castling = 0;
    for (char c : castle_str) {
        switch (c) {
            case 'K': st_.castling |= WHITE_OO;  break;
            case 'Q': st_.castling |= WHITE_OOO; break;
            case 'k': st_.castling |= BLACK_OO;  break;
            case 'q': st_.castling |= BLACK_OOO; break;
        }
    }
    st_.key ^= Zobrist::castling[st_.castling];

    if (ep_str != "-" && ep_str.size() == 2) {
        int f = ep_str[0] - 'a';
        int r = ep_str[1] - '1';
        st_.ep = Square(r * 8 + f);
        st_.key ^= Zobrist::ep_file[f];
    } else {
        st_.ep = SQ_NONE;
    }

    st_.halfmove = half;
    fullmove_ = full;
    st_.captured = NO_PIECE;
    st_.ply_from_root = 0;

    key_history_.push_back(st_.key);
    key_history_root_ = (int)key_history_.size();
    return true;
}

std::string Position::fen() const {
    std::ostringstream os;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Piece p = board_[r*8+f];
            if (p == NO_PIECE) { ++empty; continue; }
            if (empty) { os << empty; empty = 0; }
            char c = "?PNBRQK?"[type_of(p)];
            if (color_of(p) == BLACK) c = std::tolower(c);
            os << c;
        }
        if (empty) os << empty;
        if (r) os << '/';
    }
    os << (stm_ == WHITE ? " w " : " b ");
    std::string cs;
    if (st_.castling & WHITE_OO)  cs += 'K';
    if (st_.castling & WHITE_OOO) cs += 'Q';
    if (st_.castling & BLACK_OO)  cs += 'k';
    if (st_.castling & BLACK_OOO) cs += 'q';
    os << (cs.empty() ? "-" : cs) << ' ';
    os << (st_.ep == SQ_NONE ? "-" : sq_to_str(st_.ep));
    os << ' ' << st_.halfmove << ' ' << fullmove_;
    return os.str();
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    Bitboard a = 0;
    a |= BB::PawnAttacks[WHITE][s] & pieces(BLACK, PAWN);
    a |= BB::PawnAttacks[BLACK][s] & pieces(WHITE, PAWN);
    a |= BB::KnightAttacks[s] & type_bb_[KNIGHT];
    a |= BB::KingAttacks[s]   & type_bb_[KING];
    a |= BB::bishop_attacks(s, occ) & (type_bb_[BISHOP] | type_bb_[QUEEN]);
    a |= BB::rook_attacks(s, occ)   & (type_bb_[ROOK]   | type_bb_[QUEEN]);
    return a;
}

bool Position::see_ge(Move m, int threshold) const {
    static const int see_val[7] = { 0, 100, 320, 330, 500, 900, 20000 };

    // Non-normal moves: castling is always OK (>= 0), EP and promotions we
    // approximate by treating as the normal-case capture / gaining a queen.
    MoveType mt = type_of_move(m);
    if (mt == MT_CASTLING) return threshold <= 0;

    Square from = from_sq(m), to = to_sq(m);
    PieceType attacker = type_of(board_[from]);
    PieceType victim   = (mt == MT_EN_PASSANT) ? PAWN : type_of(board_[to]);
    int promo_gain = 0;
    if (mt == MT_PROMOTION) {
        promo_gain = see_val[promo_type(m)] - see_val[PAWN];
        attacker = promo_type(m);
    }

    int swap = see_val[victim] + promo_gain - threshold;
    if (swap < 0) return false;

    swap = see_val[attacker] - swap;
    if (swap <= 0) return true;

    Bitboard occ = pieces() ^ BB::sq_bb(from) ^ BB::sq_bb(to);
    if (mt == MT_EN_PASSANT)
        occ ^= BB::sq_bb(Square(int(to) + (color_of(board_[from]) == WHITE ? -8 : 8)));

    Color stm = color_of(board_[from]);
    Bitboard attackers = attackers_to(to, occ);
    int res = 1;

    while (true) {
        stm = ~stm;
        attackers &= occ;
        Bitboard our_atk = attackers & pieces(stm);
        if (!our_atk) break;

        // Find least-valuable attacker.
        int nextPt;
        Bitboard lva = 0;
        for (nextPt = PAWN; nextPt <= KING; ++nextPt) {
            lva = our_atk & type_bb_[nextPt];
            if (lva) break;
        }

        res ^= 1;

        if (nextPt == KING) {
            // If opponent still has an attacker, capturing with the king is illegal.
            if (attackers & pieces(~stm)) res ^= 1;
            break;
        }

        swap = see_val[nextPt] - swap;
        if (swap < res) break;

        // Remove this attacker from occ.
        occ ^= lva & -lva;

        // Add x-ray attackers uncovered by the removal.
        if (nextPt == PAWN || nextPt == BISHOP || nextPt == QUEEN)
            attackers |= BB::bishop_attacks(to, occ) & (type_bb_[BISHOP] | type_bb_[QUEEN]);
        if (nextPt == ROOK || nextPt == QUEEN)
            attackers |= BB::rook_attacks(to, occ) & (type_bb_[ROOK] | type_bb_[QUEEN]);
    }
    return res > 0;
}

Bitboard Position::checkers() const {
    return attackers_to(king_sq(stm_), pieces()) & pieces(~stm_);
}

Bitboard Position::pinned_pieces() const {
    Color us = stm_, them = ~us;
    Square ksq = king_sq(us);
    Bitboard occ      = pieces();
    Bitboard our_occ  = pieces(us);
    Bitboard enemy_rq = pieces(them, ROOK, QUEEN);
    Bitboard enemy_bq = pieces(them, BISHOP, QUEEN);

    // Sliders whose ray reaches the king if we removed intervening pieces.
    Bitboard snipers = (BB::rook_attacks  (ksq, 0) & enemy_rq)
                     | (BB::bishop_attacks(ksq, 0) & enemy_bq);
    Bitboard pinned = 0;
    while (snipers) {
        Square sniper = BB::pop_lsb(snipers);
        Bitboard between = BB::BetweenBB[ksq][sniper] & occ;
        if (between && (between & (between - 1)) == 0) {  // exactly one blocker
            if (between & our_occ) pinned |= between;
        }
    }
    return pinned;
}

Bitboard Position::king_danger_squares() const {
    Color us = stm_, them = ~us;
    // Occupancy WITHOUT our king — a sliding check-giver still controls squares
    // the king is trying to step to.
    Bitboard occ = pieces() ^ BB::sq_bb(king_sq(us));
    Bitboard danger = 0;

    Bitboard pawns = pieces(them, PAWN);
    while (pawns) {
        Square s = BB::pop_lsb(pawns);
        danger |= BB::PawnAttacks[them][s];
    }
    Bitboard knights = pieces(them, KNIGHT);
    while (knights) danger |= BB::KnightAttacks[BB::pop_lsb(knights)];
    Bitboard bq = pieces(them, BISHOP, QUEEN);
    while (bq) danger |= BB::bishop_attacks(BB::pop_lsb(bq), occ);
    Bitboard rq = pieces(them, ROOK, QUEEN);
    while (rq) danger |= BB::rook_attacks(BB::pop_lsb(rq), occ);
    danger |= BB::KingAttacks[king_sq(them)];
    return danger;
}

bool Position::is_square_attacked(Square s, Color by) const {
    Bitboard occ = pieces();
    if (BB::PawnAttacks[~by][s] & pieces(by, PAWN)) return true;
    if (BB::KnightAttacks[s]    & pieces(by, KNIGHT)) return true;
    if (BB::KingAttacks[s]      & pieces(by, KING)) return true;
    if (BB::bishop_attacks(s, occ) & pieces(by, BISHOP, QUEEN)) return true;
    if (BB::rook_attacks(s, occ)   & pieces(by, ROOK,   QUEEN)) return true;
    return false;
}

int Position::non_pawn_material(Color c) const {
    static const int vals[7] = { 0, 0, 320, 330, 500, 900, 0 };
    int sum = 0;
    for (int pt = KNIGHT; pt <= QUEEN; ++pt)
        sum += BB::popcount(pieces(c, PieceType(pt))) * vals[pt];
    return sum;
}

void Position::update_castling_on_touch(Square s) {
    int new_rights = st_.castling & CastlingMask[s];
    if (new_rights != st_.castling) {
        st_.key ^= Zobrist::castling[st_.castling];
        st_.castling = new_rights;
        st_.key ^= Zobrist::castling[st_.castling];
    }
}

void Position::do_move(Move m) {
    history_.push_back(st_);
    st_.ply_from_root++;

    Square from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of_move(m);
    Piece moved = board_[from];
    Piece captured = (mt == MT_EN_PASSANT)
                   ? make_piece(~stm_, PAWN)
                   : board_[to];
    st_.captured = captured;

    // Clear ep from hash (we set a new one below if needed)
    if (st_.ep != SQ_NONE) {
        st_.key ^= Zobrist::ep_file[file_of(st_.ep)];
        st_.ep = SQ_NONE;
    }

    // Halfmove clock
    if (type_of(moved) == PAWN || captured != NO_PIECE)
        st_.halfmove = 0;
    else
        st_.halfmove++;

    // Handle capture (remove captured piece from its square)
    if (captured != NO_PIECE) {
        Square cap_sq = to;
        if (mt == MT_EN_PASSANT)
            cap_sq = Square(to + (stm_ == WHITE ? -8 : 8));
        remove_piece(cap_sq);
    }

    // Castling rook move
    if (mt == MT_CASTLING) {
        Square rook_from, rook_to;
        if (to > from) { // kingside
            rook_from = Square(to + 1);
            rook_to   = Square(to - 1);
        } else {         // queenside
            rook_from = Square(to - 2);
            rook_to   = Square(to + 1);
        }
        move_piece(rook_from, rook_to);
    }

    // Move the piece
    move_piece(from, to);

    // Promotion: replace pawn with promoted piece
    if (mt == MT_PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(stm_, promo_type(m)), to);
    }

    // Double pawn push sets ep target
    if (type_of(moved) == PAWN && std::abs(int(to) - int(from)) == 16) {
        Square ep = Square((int(to) + int(from)) / 2);
        st_.ep = ep;
        st_.key ^= Zobrist::ep_file[file_of(ep)];
    }

    // Update castling rights on either touched square
    update_castling_on_touch(from);
    update_castling_on_touch(to);

    // Flip side
    stm_ = ~stm_;
    st_.key ^= Zobrist::side;
    if (stm_ == WHITE) fullmove_++;

    key_history_.push_back(st_.key);
}

void Position::undo_move(Move m) {
    key_history_.pop_back();

    stm_ = ~stm_;
    if (stm_ == BLACK) fullmove_--;

    Square from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of_move(m);

    // Undo promotion: turn promoted piece back into pawn
    if (mt == MT_PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(stm_, PAWN), to);
    }

    move_piece(to, from);

    // Undo castling rook move
    if (mt == MT_CASTLING) {
        Square rook_from, rook_to;
        if (to > from) {
            rook_from = Square(to + 1);
            rook_to   = Square(to - 1);
        } else {
            rook_from = Square(to - 2);
            rook_to   = Square(to + 1);
        }
        move_piece(rook_to, rook_from);
    }

    // Restore captured piece
    Piece captured = st_.captured;
    if (captured != NO_PIECE) {
        Square cap_sq = to;
        if (mt == MT_EN_PASSANT)
            cap_sq = Square(to + (stm_ == WHITE ? -8 : 8));
        put_piece(captured, cap_sq);
    }

    // Restore prior state (this sets key, castling, ep, halfmove back)
    st_ = history_.back();
    history_.pop_back();
}

void Position::do_null_move() {
    history_.push_back(st_);
    st_.ply_from_root++;

    if (st_.ep != SQ_NONE) {
        st_.key ^= Zobrist::ep_file[file_of(st_.ep)];
        st_.ep = SQ_NONE;
    }
    st_.halfmove++;
    st_.captured = NO_PIECE;

    stm_ = ~stm_;
    st_.key ^= Zobrist::side;
    if (stm_ == WHITE) fullmove_++;
    key_history_.push_back(st_.key);
}

void Position::undo_null_move() {
    key_history_.pop_back();
    stm_ = ~stm_;
    if (stm_ == BLACK) fullmove_--;
    st_ = history_.back();
    history_.pop_back();
}

bool Position::is_legal(Move m) const {
    // Make a mutable copy approach would be expensive; emulate by temp do/undo.
    // To avoid const_cast complexity, check legality manually for king moves and
    // for other moves use pin/check-aware simple rules.
    Position& p = const_cast<Position&>(*this);
    p.do_move(m);
    bool ok = !p.is_square_attacked(p.king_sq(~p.stm_), p.stm_);
    p.undo_move(m);
    return ok;
}

bool Position::is_repetition_or_50move() const {
    if (st_.halfmove >= 100) return true;
    // Count how many times current key appeared in history since last irreversible move.
    // We walk back up to halfmove plies; if we see our key once more in that range, call it a draw.
    int cnt = 0;
    int n = (int)key_history_.size();
    int limit = std::min(n - 1, st_.halfmove);
    // The last entry in key_history_ is our current key (pushed after do_move or set_fen).
    U64 k = st_.key;
    for (int i = n - 3; i >= n - 1 - limit && i >= 0; i -= 2) {
        if (key_history_[i] == k) {
            if (++cnt >= 1) return true;
        }
    }
    return false;
}

std::string Position::move_to_uci(Move m) const {
    if (m == MOVE_NONE) return "0000";
    std::string s = sq_to_str(from_sq(m)) + sq_to_str(to_sq(m));
    if (type_of_move(m) == MT_PROMOTION) {
        char c = "nbrq"[promo_type(m) - KNIGHT];
        s += c;
    }
    return s;
}

Move Position::parse_uci(const std::string& s) const {
    if (s.size() < 4) return MOVE_NONE;
    Square from = Square((s[0] - 'a') + (s[1] - '1') * 8);
    Square to   = Square((s[2] - 'a') + (s[3] - '1') * 8);
    PieceType promo = KNIGHT;
    bool is_promo = false;
    if (s.size() >= 5) {
        is_promo = true;
        switch (s[4]) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK; break;
            case 'q': promo = QUEEN; break;
            default: return MOVE_NONE;
        }
    }
    Piece pc = board_[from];
    if (pc == NO_PIECE) return MOVE_NONE;
    if (type_of(pc) == KING && std::abs(int(to) - int(from)) == 2)
        return make_move(from, to, MT_CASTLING);
    if (is_promo)
        return make_move(from, to, MT_PROMOTION, promo);
    if (type_of(pc) == PAWN && to == st_.ep)
        return make_move(from, to, MT_EN_PASSANT);
    return make_move(from, to);
}

void Position::print() const {
    static const char* syms = ".PNBRQK?pnbrqk?";
    for (int r = 7; r >= 0; --r) {
        std::printf("%d ", r + 1);
        for (int f = 0; f < 8; ++f) {
            Piece p = board_[r*8+f];
            char c = syms[p & 0xF];
            std::printf("%c ", c);
        }
        std::printf("\n");
    }
    std::printf("  a b c d e f g h\n");
    std::printf("FEN: %s\n", fen().c_str());
    std::printf("Key: %016llx\n", (unsigned long long)st_.key);
}

bool Position::gives_check(Move m) const {
    // Simple approach: make/unmake and test.
    Position& p = const_cast<Position&>(*this);
    p.do_move(m);
    bool chk = p.is_square_attacked(p.king_sq(p.stm_), ~p.stm_);
    p.undo_move(m);
    return chk;
}
