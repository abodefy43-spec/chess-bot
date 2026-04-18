#include "movegen.h"

using BB::sq_bb;
using BB::popcount;
using BB::lsb;
using BB::pop_lsb;

static void gen_pawn_moves(const Position& pos, MoveList& list, GenType gt) {
    Color us = pos.side_to_move(), them = ~us;
    Bitboard our_pawns = pos.pieces(us, PAWN);
    Bitboard enemies   = pos.pieces(them);
    Bitboard empty     = ~pos.pieces();
    int dir    = (us == WHITE) ?  8 : -8;
    Bitboard promo_rank = (us == WHITE) ? BB::RANK_7 : BB::RANK_2;  // pawns here promote on push
    Bitboard start_rank = (us == WHITE) ? BB::RANK_2 : BB::RANK_7;  // pawns here can double-push

    // Pawns that will promote vs not
    Bitboard promo_pawns = our_pawns & promo_rank;
    Bitboard push_pawns  = our_pawns & ~promo_rank;

    // Single & double pushes (quiet)
    if (gt == GEN_ALL) {
        Bitboard one = (us == WHITE) ? BB::shift<BB::NORTH>(push_pawns) & empty
                                     : BB::shift<BB::SOUTH>(push_pawns) & empty;
        Bitboard src = one;
        while (src) {
            Square to = pop_lsb(src);
            list.add(make_move(Square(int(to) - dir), to));
        }
        Bitboard p2 = push_pawns & start_rank;
        Bitboard d1 = (us == WHITE) ? BB::shift<BB::NORTH>(p2) & empty
                                    : BB::shift<BB::SOUTH>(p2) & empty;
        Bitboard d2 = (us == WHITE) ? BB::shift<BB::NORTH>(d1) & empty
                                    : BB::shift<BB::SOUTH>(d1) & empty;
        src = d2;
        while (src) {
            Square to = pop_lsb(src);
            list.add(make_move(Square(int(to) - 2*dir), to));
        }
    }

    // Captures (non-promotion pawns)
    Bitboard cap_w = (us == WHITE) ? BB::shift<BB::NW>(push_pawns) & enemies
                                   : BB::shift<BB::SW>(push_pawns) & enemies;
    Bitboard cap_e = (us == WHITE) ? BB::shift<BB::NE>(push_pawns) & enemies
                                   : BB::shift<BB::SE>(push_pawns) & enemies;
    int dw = (us == WHITE) ? 7 : -9;
    int de = (us == WHITE) ? 9 : -7;
    while (cap_w) {
        Square to = pop_lsb(cap_w);
        list.add(make_move(Square(int(to) - dw), to));
    }
    while (cap_e) {
        Square to = pop_lsb(cap_e);
        list.add(make_move(Square(int(to) - de), to));
    }

    // Promotions
    if (promo_pawns) {
        Bitboard pushes = (us == WHITE) ? BB::shift<BB::NORTH>(promo_pawns) & empty
                                        : BB::shift<BB::SOUTH>(promo_pawns) & empty;
        Bitboard pc_w   = (us == WHITE) ? BB::shift<BB::NW>(promo_pawns) & enemies
                                        : BB::shift<BB::SW>(promo_pawns) & enemies;
        Bitboard pc_e   = (us == WHITE) ? BB::shift<BB::NE>(promo_pawns) & enemies
                                        : BB::shift<BB::SE>(promo_pawns) & enemies;

        auto emit_promos = [&](Bitboard bb, int delta) {
            while (bb) {
                Square to = pop_lsb(bb);
                Square from = Square(int(to) - delta);
                if (gt == GEN_ALL) {
                    list.add(make_move(from, to, MT_PROMOTION, QUEEN));
                    list.add(make_move(from, to, MT_PROMOTION, ROOK));
                    list.add(make_move(from, to, MT_PROMOTION, BISHOP));
                    list.add(make_move(from, to, MT_PROMOTION, KNIGHT));
                } else {
                    // In captures-only mode, include queen promotions (most important)
                    list.add(make_move(from, to, MT_PROMOTION, QUEEN));
                    list.add(make_move(from, to, MT_PROMOTION, KNIGHT));
                }
            }
        };
        if (gt == GEN_ALL) emit_promos(pushes, dir);
        emit_promos(pc_w, dw);
        emit_promos(pc_e, de);
    }

    // En passant
    Square ep = pos.ep_square();
    if (ep != SQ_NONE) {
        Bitboard attackers = BB::PawnAttacks[them][ep] & our_pawns;
        while (attackers) {
            Square from = pop_lsb(attackers);
            list.add(make_move(from, ep, MT_EN_PASSANT));
        }
    }
}

template<PieceType Pt>
static void gen_piece_moves(const Position& pos, MoveList& list, GenType gt) {
    Color us = pos.side_to_move();
    Bitboard pieces = pos.pieces(us, Pt);
    Bitboard targets = (gt == GEN_CAPTURES) ? pos.pieces(~us) : ~pos.pieces(us);
    Bitboard occ = pos.pieces();
    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard moves = BB::attacks_of(Pt, from, occ) & targets;
        while (moves) {
            Square to = pop_lsb(moves);
            list.add(make_move(from, to));
        }
    }
}

static void gen_castling(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    Square ksq = pos.king_sq(us);
    if (pos.is_square_attacked(ksq, ~us)) return;

    int rights = pos.castling_rights();
    Bitboard occ = pos.pieces();

    if (us == WHITE) {
        if ((rights & WHITE_OO) &&
            !(occ & (sq_bb(F1) | sq_bb(G1))) &&
            !pos.is_square_attacked(F1, BLACK) &&
            !pos.is_square_attacked(G1, BLACK))
            list.add(make_move(E1, G1, MT_CASTLING));
        if ((rights & WHITE_OOO) &&
            !(occ & (sq_bb(B1) | sq_bb(C1) | sq_bb(D1))) &&
            !pos.is_square_attacked(D1, BLACK) &&
            !pos.is_square_attacked(C1, BLACK))
            list.add(make_move(E1, C1, MT_CASTLING));
    } else {
        if ((rights & BLACK_OO) &&
            !(occ & (sq_bb(F8) | sq_bb(G8))) &&
            !pos.is_square_attacked(F8, WHITE) &&
            !pos.is_square_attacked(G8, WHITE))
            list.add(make_move(E8, G8, MT_CASTLING));
        if ((rights & BLACK_OOO) &&
            !(occ & (sq_bb(B8) | sq_bb(C8) | sq_bb(D8))) &&
            !pos.is_square_attacked(D8, WHITE) &&
            !pos.is_square_attacked(C8, WHITE))
            list.add(make_move(E8, C8, MT_CASTLING));
    }
}

void generate_moves(const Position& pos, MoveList& list, GenType gt) {
    gen_pawn_moves(pos, list, gt);
    gen_piece_moves<KNIGHT>(pos, list, gt);
    gen_piece_moves<BISHOP>(pos, list, gt);
    gen_piece_moves<ROOK>  (pos, list, gt);
    gen_piece_moves<QUEEN> (pos, list, gt);
    gen_piece_moves<KING>  (pos, list, gt);
    if (gt == GEN_ALL) gen_castling(pos, list);
}
