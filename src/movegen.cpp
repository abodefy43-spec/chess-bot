#include "movegen.h"

using BB::sq_bb;
using BB::popcount;
using BB::lsb;
using BB::pop_lsb;

namespace {

// Emit a pawn move, expanding promotions if the target is on the 1st/8th rank.
inline void emit_pawn(MoveList& list, Square from, Square to, GenType gt) {
    int to_rank = to >> 3;
    if (to_rank == 0 || to_rank == 7) {
        if (gt == GEN_ALL) {
            list.add(make_move(from, to, MT_PROMOTION, QUEEN));
            list.add(make_move(from, to, MT_PROMOTION, ROOK));
            list.add(make_move(from, to, MT_PROMOTION, BISHOP));
            list.add(make_move(from, to, MT_PROMOTION, KNIGHT));
        } else {
            list.add(make_move(from, to, MT_PROMOTION, QUEEN));
            list.add(make_move(from, to, MT_PROMOTION, KNIGHT));
        }
    } else {
        list.add(make_move(from, to));
    }
}

// Does this en-passant capture leave our king in check? The unusual case where
// removing both the capturing pawn AND the captured pawn opens a rook/queen ray
// onto our king along the 4th/5th rank. Called only for the few EP candidates.
bool ep_is_legal(const Position& pos, Square from, Square to, Color us) {
    Square captured_sq = Square(int(to) + (us == WHITE ? -8 : 8));
    Square ksq = pos.king_sq(us);
    Bitboard occ = pos.pieces();
    occ ^= sq_bb(from) | sq_bb(captured_sq);
    occ |= sq_bb(to);
    Color them = (us == WHITE ? BLACK : WHITE);
    if (BB::rook_attacks  (ksq, occ) & pos.pieces(them, ROOK,   QUEEN)) return false;
    if (BB::bishop_attacks(ksq, occ) & pos.pieces(them, BISHOP, QUEEN)) return false;
    return true;
}

// King moves: attacks & ~own & ~danger.
void gen_king_moves(const Position& pos, MoveList& list, GenType gt,
                    Bitboard danger) {
    Color us = pos.side_to_move();
    Square ksq = pos.king_sq(us);
    Bitboard targets = BB::KingAttacks[ksq] & ~pos.pieces(us) & ~danger;
    if (gt == GEN_CAPTURES) targets &= pos.pieces(~us);
    while (targets) {
        Square to = pop_lsb(targets);
        list.add(make_move(ksq, to));
    }
}

void gen_castling(const Position& pos, MoveList& list, Bitboard danger) {
    Color us = pos.side_to_move();
    Square ksq = pos.king_sq(us);
    if (danger & sq_bb(ksq)) return;  // can't castle out of check
    int rights = pos.castling_rights();
    Bitboard occ = pos.pieces();

    auto try_castle = [&](int right, Bitboard path_empty, Bitboard path_safe,
                          Square from, Square to) {
        if (!(rights & right)) return;
        if (occ & path_empty) return;
        if (danger & path_safe) return;
        list.add(make_move(from, to, MT_CASTLING));
    };

    if (us == WHITE) {
        try_castle(WHITE_OO,  sq_bb(F1) | sq_bb(G1), sq_bb(F1) | sq_bb(G1), E1, G1);
        try_castle(WHITE_OOO, sq_bb(B1) | sq_bb(C1) | sq_bb(D1), sq_bb(C1) | sq_bb(D1), E1, C1);
    } else {
        try_castle(BLACK_OO,  sq_bb(F8) | sq_bb(G8), sq_bb(F8) | sq_bb(G8), E8, G8);
        try_castle(BLACK_OOO, sq_bb(B8) | sq_bb(C8) | sq_bb(D8), sq_bb(C8) | sq_bb(D8), E8, C8);
    }
}

// Generate pawn moves for non-pinned pawns (mask_target filters the destination).
// Pinned pawns are handled separately.
void gen_pawn_moves(const Position& pos, MoveList& list, GenType gt,
                    Bitboard target, Bitboard pinned) {
    Color us = pos.side_to_move(), them = ~us;
    Bitboard our_pawns = pos.pieces(us, PAWN);
    Bitboard free_pawns = our_pawns & ~pinned;
    Bitboard empty   = ~pos.pieces();
    Bitboard enemies = pos.pieces(them);

    int push  = (us == WHITE ? 8 : -8);
    int capNW = (us == WHITE ? 7 : -9);
    int capNE = (us == WHITE ? 9 : -7);
    Bitboard start_rank = (us == WHITE ? BB::RANK_2 : BB::RANK_7);

    // Quiet pushes (including promotions on push).
    if (gt == GEN_ALL) {
        Bitboard one = (us == WHITE) ? BB::shift<BB::NORTH>(free_pawns) & empty
                                     : BB::shift<BB::SOUTH>(free_pawns) & empty;
        Bitboard two = (us == WHITE) ? BB::shift<BB::NORTH>(one & (BB::RANK_2 << 8)) & empty
                                     : BB::shift<BB::SOUTH>(one & (BB::RANK_7 >> 8)) & empty;
        Bitboard one_t = one & target;
        while (one_t) {
            Square to = pop_lsb(one_t);
            emit_pawn(list, Square(int(to) - push), to, gt);
        }
        Bitboard two_t = two & target;
        while (two_t) {
            Square to = pop_lsb(two_t);
            Square from = Square(int(to) - 2 * push);
            if (sq_bb(from) & free_pawns & start_rank)
                list.add(make_move(from, to));
        }
    } else {
        // Captures-only mode still needs promotion pushes (queen/knight promos).
        Bitboard promo_pawns = free_pawns & (us == WHITE ? BB::RANK_7 : BB::RANK_2);
        Bitboard one = (us == WHITE) ? BB::shift<BB::NORTH>(promo_pawns) & empty
                                     : BB::shift<BB::SOUTH>(promo_pawns) & empty;
        Bitboard one_t = one & target;
        while (one_t) {
            Square to = pop_lsb(one_t);
            emit_pawn(list, Square(int(to) - push), to, gt);
        }
    }

    // Captures.
    Bitboard capsW = (us == WHITE) ? BB::shift<BB::NW>(free_pawns) & enemies
                                   : BB::shift<BB::SW>(free_pawns) & enemies;
    Bitboard capsE = (us == WHITE) ? BB::shift<BB::NE>(free_pawns) & enemies
                                   : BB::shift<BB::SE>(free_pawns) & enemies;
    capsW &= target;
    capsE &= target;
    while (capsW) {
        Square to = pop_lsb(capsW);
        emit_pawn(list, Square(int(to) - capNW), to, gt);
    }
    while (capsE) {
        Square to = pop_lsb(capsE);
        emit_pawn(list, Square(int(to) - capNE), to, gt);
    }

    // Pinned pawns: only moves along their pin ray are legal.
    Bitboard pinned_pawns = our_pawns & pinned;
    Square ksq = pos.king_sq(us);
    while (pinned_pawns) {
        Square from = pop_lsb(pinned_pawns);
        Bitboard pin_line = BB::LineBB[ksq][from];
        // Pushes
        if (gt == GEN_ALL) {
            Square to1 = Square(int(from) + push);
            if (to1 >= A1 && to1 <= H8 && (empty & sq_bb(to1)) &&
                (pin_line & sq_bb(to1)) && (target & sq_bb(to1))) {
                emit_pawn(list, from, to1, gt);
                // double push
                if (sq_bb(from) & start_rank) {
                    Square to2 = Square(int(to1) + push);
                    if ((empty & sq_bb(to2)) && (pin_line & sq_bb(to2)) &&
                        (target & sq_bb(to2)))
                        list.add(make_move(from, to2));
                }
            }
        }
        // Captures (incl promotion)
        Bitboard caps = BB::PawnAttacks[us][from] & enemies & pin_line & target;
        while (caps) {
            Square to = pop_lsb(caps);
            emit_pawn(list, from, to, gt);
        }
    }

    // En passant. Rare enough that we check legality inline with occupancy swap.
    Square ep = pos.ep_square();
    if (ep != SQ_NONE) {
        // Must also be valid under the single-check evasion mask: capturing the
        // checker pawn, or blocking with the EP destination.
        Square ep_victim = Square(int(ep) + (us == WHITE ? -8 : 8));
        bool evasion_ok = (target & sq_bb(ep)) || (target & sq_bb(ep_victim));
        if (evasion_ok) {
            Bitboard attackers = BB::PawnAttacks[them][ep] & our_pawns;
            while (attackers) {
                Square from = pop_lsb(attackers);
                if (ep_is_legal(pos, from, ep, us))
                    list.add(make_move(from, ep, MT_EN_PASSANT));
            }
        }
    }
}

// Knight/bishop/rook/queen moves.
template<PieceType Pt>
void gen_piece_moves(const Position& pos, MoveList& list, Bitboard target,
                     Bitboard pinned) {
    Color us = pos.side_to_move();
    Bitboard movers = pos.pieces(us, Pt);
    Bitboard occ = pos.pieces();
    Square ksq = pos.king_sq(us);

    // Knights can never move when pinned (their L-shape moves never stay on a line).
    if (Pt == KNIGHT) movers &= ~pinned;

    while (movers) {
        Square from = pop_lsb(movers);
        Bitboard moves = BB::attacks_of(Pt, from, occ) & target;
        if (Pt != KNIGHT && (pinned & sq_bb(from)))
            moves &= BB::LineBB[ksq][from];
        while (moves) {
            Square to = pop_lsb(moves);
            list.add(make_move(from, to));
        }
    }
}

} // anonymous namespace

void generate_moves(const Position& pos, MoveList& list, GenType gt) {
    Color us = pos.side_to_move();
    Bitboard checkers = pos.checkers();
    Bitboard pinned   = pos.pinned_pieces();
    Bitboard danger   = pos.king_danger_squares();

    gen_king_moves(pos, list, gt, danger);

    // Double check: only king moves are legal.
    if (checkers && (checkers & (checkers - 1))) return;

    // Target mask for non-king moves.
    Bitboard target;
    if (checkers) {
        Square checker_sq = BB::lsb(checkers);
        target = checkers | BB::BetweenBB[pos.king_sq(us)][checker_sq];
    } else {
        target = ~pos.pieces(us);
    }
    if (gt == GEN_CAPTURES) target &= pos.pieces(~us);

    gen_piece_moves<KNIGHT>(pos, list, target, pinned);
    gen_piece_moves<BISHOP>(pos, list, target, pinned);
    gen_piece_moves<ROOK>  (pos, list, target, pinned);
    gen_piece_moves<QUEEN> (pos, list, target, pinned);
    gen_pawn_moves         (pos, list, gt, target, pinned);

    if (!checkers && gt == GEN_ALL) gen_castling(pos, list, danger);
}
