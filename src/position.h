#pragma once
#include "types.h"
#include "bitboard.h"
#include <array>
#include <string>
#include <vector>

struct StateInfo {
    U64      key;
    int      castling;
    Square   ep;
    int      halfmove;
    Piece    captured;
    int      ply_from_root;   // set during search
};

class Position {
public:
    Position() { clear(); }

    void clear();
    void set_startpos();
    bool set_fen(const std::string& fen);
    std::string fen() const;

    // Accessors
    Piece    piece_on(Square s) const { return board_[s]; }
    Bitboard pieces() const             { return occ_[WHITE] | occ_[BLACK]; }
    Bitboard pieces(Color c) const      { return occ_[c]; }
    Bitboard pieces(PieceType pt) const { return type_bb_[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return occ_[c] & type_bb_[pt]; }
    Bitboard pieces(Color c, PieceType pt1, PieceType pt2) const {
        return occ_[c] & (type_bb_[pt1] | type_bb_[pt2]);
    }
    Color side_to_move() const { return stm_; }
    int  castling_rights() const { return st_.castling; }
    Square ep_square() const { return st_.ep; }
    int halfmove() const { return st_.halfmove; }
    int fullmove() const { return fullmove_; }
    U64  key() const { return st_.key; }
    int  ply_from_root() const { return st_.ply_from_root; }
    void set_ply_from_root(int p) { st_.ply_from_root = p; }
    Square king_sq(Color c) const { return BB::lsb(pieces(c, KING)); }
    int non_pawn_material(Color c) const;

    // Attack/check queries
    bool is_square_attacked(Square s, Color by) const;
    Bitboard attackers_to(Square s, Bitboard occ) const;
    bool in_check() const { return is_square_attacked(king_sq(stm_), ~stm_); }
    bool gives_check(Move m) const;  // after making the move

    // Move manipulation
    void do_move(Move m);
    void undo_move(Move m);
    void do_null_move();
    void undo_null_move();

    // Legality for a pseudo-legal move
    bool is_legal(Move m) const;

    // Repetition / 50-move
    bool is_repetition_or_50move() const;

    std::string move_to_uci(Move m) const;
    Move parse_uci(const std::string& s) const;

    // Debug
    void print() const;

private:
    std::array<Piece, 64>    board_;
    std::array<Bitboard, 7>  type_bb_;   // index by PieceType (0 unused)
    std::array<Bitboard, 2>  occ_;
    Color stm_;
    int   fullmove_;
    StateInfo st_;

    // Stack of prior states (for undo) and key history (for repetition detection)
    std::vector<StateInfo> history_;
    std::vector<U64>       key_history_;
    int key_history_root_;  // size at root of search, for 50-move compare

    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void update_castling_on_touch(Square s);
};
